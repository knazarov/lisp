#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

#define SLAB_SIZE 1024
#define TOKEN_BUF_SIZE 256
#define GC_THRESHOLD 1
#define GC_ROOT_STACK_SIZE 1024

enum type_t {
  GUARD = 0,
  SYMBOL,
  CONS,
  INT,
  PROC,
  PRIMITIVE,
  MACRO
};

struct value_t;


typedef struct value_t* (*primitive_op_t)(struct value_t*);

struct cons_t {
  struct value_t* car;
  struct value_t* cdr;
};

struct symbol_t {
  char* name;
};

struct proc_t {
  struct value_t* params;
  struct value_t* body;
  struct value_t* env;
};

struct value_t {
  enum type_t type;
  char gc_flag;

  union {
    struct cons_t cons;
    struct symbol_t symbol;
    struct proc_t proc;
    long int_value;
    primitive_op_t primitive_op;
  };
};


struct memory_slab_t {
  struct value_t data[SLAB_SIZE];
  char used_blocks[SLAB_SIZE];
  char reachable_blocks[SLAB_SIZE];
  struct memory_slab_t* parent;
};

size_t number_of_allocations = 0;
size_t last_allocations = 0;

struct memory_slab_t* toplevel_slab = 0;
struct value_t* gc_root_stack[GC_ROOT_STACK_SIZE];
size_t gc_root_stack_pos = 0;

#define DEFSYM(symname) \
  struct value_t* symname##_p = 0;

#define REGISTER_SYMBOL(symname) \
  symname##_p = slab_alloc(); \
  *symname##_p = (struct value_t){.type=SYMBOL, .symbol.name = #symname };  \
  symbols = cons(symname##_p, symbols);

#define CHECK_GUARD(val) \
  if ((val)->type == GUARD) die("Access to deallocated memory");

DEFSYM(nil);
DEFSYM(t);
DEFSYM(quote);
DEFSYM(if);
DEFSYM(lambda);
DEFSYM(progn);
DEFSYM(cons);
DEFSYM(car);
DEFSYM(cdr);
DEFSYM(setf);
DEFSYM(define);
DEFSYM(defmacro);

struct value_t *symbols = 0;
struct value_t *toplevel_env = 0;
struct value_t *toplevel_code = 0;

char token_buf[TOKEN_BUF_SIZE];
size_t token_buf_used = 0;


int die(const char *format, ...)
{
    va_list args;
    va_start(args, format);

    vprintf(format, args);

    va_end(args);

    exit(1);
}

struct value_t* slab_alloc() {
  struct memory_slab_t* slab;
  for (slab = toplevel_slab; slab != 0; slab = slab->parent) {
    for (size_t i = 0; i<SLAB_SIZE; ++i) {
      if (slab->used_blocks[i] == 0) {
        slab->used_blocks[i] = 1;
        number_of_allocations++;
        last_allocations++;
        slab->data[i].gc_flag = 0;
        return &slab->data[i];
      }
    }
  }

  struct memory_slab_t* new_slab = malloc(sizeof(struct memory_slab_t));
  memset(new_slab, 0, sizeof(struct memory_slab_t));
  new_slab->parent = toplevel_slab;
  toplevel_slab = new_slab;

  return slab_alloc();
}

struct value_t *cons(struct value_t* car, struct value_t* cdr) {
  struct value_t *ret = slab_alloc();

  *ret = (struct value_t){.type = CONS, .cons.car = car, .cons.cdr = cdr};

  return ret;
}

int is_nil(struct value_t* val) {
  return val == nil_p;
}

struct value_t *car(struct value_t* val) {
  CHECK_GUARD(val);

  if (val == nil_p)
    return nil_p;

  return val->cons.car;
}

struct value_t *cdr(struct value_t* val) {
  CHECK_GUARD(val);

  if (val == nil_p)
    return nil_p;

  return val->cons.cdr;
}

size_t memory_used() {
  size_t res = 0;
  struct memory_slab_t* slab;
  for (slab = toplevel_slab; slab != 0; slab = slab->parent) {
    for (size_t i = 0; i < SLAB_SIZE; i++) {
      if (slab->used_blocks[i] == 1)
        res++;
    }
  }
  return res;
}

void slab_free(struct value_t* val) {
  struct memory_slab_t* slab;
  for (slab = toplevel_slab; slab != 0; slab = slab->parent) {
    if (val >= slab->data && val <= &slab->data[SLAB_SIZE]) {
      size_t pos = (val - slab->data);
      slab->used_blocks[pos] = 0;
      memset(&slab->data[pos], 0, sizeof(struct value_t));

      return;
    }
  }

  die("Can't free memory");
}

void gc_root_push(struct value_t* val) {
  gc_root_stack[gc_root_stack_pos++] = val;
  if (gc_root_stack_pos >= GC_ROOT_STACK_SIZE)
    die("Out of gc root stack");
}

void gc_root_pop() {
  gc_root_stack_pos--;
}


void gc_mark_val(struct value_t* val) {
  if (val->gc_flag == 1)
    return;

  val->gc_flag = 1;

  struct value_t* tmp;
  switch(val->type) {
  case GUARD:
    die("Access to deallocated memory");
    break;
  case CONS:
    for (tmp=val; tmp != nil_p; tmp = cdr(tmp)) {
      if (tmp->type == CONS) {
        tmp->gc_flag = 1;
        gc_mark_val(car(tmp));
      }
      else {
        gc_mark_val(tmp);
        break;
      }
    }
    break;
  case MACRO:
  case PROC:
    gc_mark_val(val->proc.params);
    gc_mark_val(val->proc.body);
    gc_mark_val(val->proc.env);
    break;
  default:
    break;
  };
}

void gc_mark() {
  for (size_t i=0; i<gc_root_stack_pos; i++) {
    gc_mark_val(gc_root_stack[i]);
  }
}

int need_gc() {
  return last_allocations > GC_THRESHOLD;
}

void gc_sweep() {
  struct memory_slab_t* slab;
  for (slab = toplevel_slab; slab != 0; slab = slab->parent) {
    for (size_t i = 0; i < SLAB_SIZE; i++) {
      if (slab->used_blocks[i] == 1 && slab->data[i].gc_flag == 0) {
        slab->used_blocks[i] = 0;
        memset(&slab->data[i], 0, sizeof(struct value_t));
      }
      else
        slab->data[i].gc_flag = 0;
    }
  }

  last_allocations = 0;
}

void collectgarbage() {
  gc_mark();
  gc_sweep();
}

char *strdup(const char *s) {
    size_t size = strlen(s) + 1;
    char *p = malloc(size);
    if (p) {
        memcpy(p, s, size);
    }
    return p;
}

char *ltoa(long val) {
  char buf[32];

  sprintf(buf, "%li", val);
  return strdup(buf);
}


struct value_t* makeint(long val) {
  struct value_t *ret = slab_alloc();
  *ret = (struct value_t){.type = INT, .int_value = val};

  return ret;
}

struct value_t* makesym(const char* name) {
  struct value_t *ret = slab_alloc();
  *ret = (struct value_t){.type = SYMBOL, .symbol.name = strdup(name)};

  return ret;
}

struct value_t* makeprimitive(primitive_op_t op) {
  struct value_t *ret = slab_alloc();
  *ret = (struct value_t){.type = PRIMITIVE, .primitive_op = op};

  return ret;
}

struct value_t* makeproc(struct value_t* params,
                         struct value_t* body,
                         struct value_t * env) {
  struct value_t *ret = slab_alloc();
  *ret = (struct value_t){.type = PROC,
                          .proc.params = params,
                          .proc.body = body,
                          .proc.env = env};

  return ret;
}

struct value_t* makemacro(struct value_t* params,
                          struct value_t* body,
                          struct value_t * env) {
  struct value_t *ret = makeproc(params, body, env);
  ret->type = MACRO;
  return ret;
}


struct value_t *makeenv(struct value_t* parent) {
  struct value_t *ret = slab_alloc();
  *ret = (struct value_t){.type = CONS,
                          .cons.car = nil_p,
                          .cons.cdr = parent};
  return ret;
}

struct value_t* find_symbol(const char* name) {
  struct value_t* sym;
  for (sym = symbols; !is_nil(sym); sym = cdr(sym)) {
    if (strcmp(name, car(sym)->symbol.name) == 0)
      return car(sym);
  }
  return 0;
}

struct value_t* intern(const char* name) {
  struct value_t* sym = find_symbol(name);

  if (sym != 0)
    return sym;

  sym = makesym(name);

  symbols = cons(sym, symbols);
  return sym;
}



void add_to_token_buf(char c) {
  token_buf[token_buf_used++] = c;
}

const char* token_buf_to_str() {
  add_to_token_buf('\0');
  return token_buf;
}

const char* gettoken(const char** strp) {
  const char* p = *strp;
  token_buf_used = 0;

  do {
    if (isspace(*p))
      ++p;
  } while(isspace(*p));

  if (*p == '\0') {
    return 0;
  }


  add_to_token_buf(*p);
  if (*p == '(' || *p == ')' || *p == '\'') {
    *strp = p+1;
    return token_buf_to_str();
  }
  p++;

  for (;; ++p) {
    if (*p == '\0') {
      *strp = p;

      if (token_buf_used == 0)
        return 0;
      else
        return token_buf_to_str();
    }

    if (*p == '(' || *p == ')' || *p == '\'' || isspace(*p)) {
      *strp = p;
      return token_buf_to_str();
    }

    add_to_token_buf(*p);
  }
}


int is_number(const char* str) {
  char* endptr;
  strtol(str, &endptr, 10);

  if (*endptr == '\0')
    return 1;

  return 0;
}

struct value_t* readobj(const char** strp);

struct value_t* readlist(const char** strp) {
  const char* saved = *strp;
  const char* token = gettoken(strp);

  if (token == 0)
    die("Malformed list");

  if (strcmp(token, ")") == 0) {
    return nil_p;
  }

  *strp = saved;

  struct value_t* head = readobj(strp);
  struct value_t* tail = readlist(strp);

  return cons(head, tail);
}


struct value_t* readobj(const char** strp) {
  const char* token = gettoken(strp);

  if (token == 0)
    return nil_p;

  if (strcmp(token, "(") == 0) {
    return readlist(strp);
  }

  if (strcmp(token, "\'") == 0) {
    return cons(quote_p, cons(readobj(strp), nil_p));
  }

  if (is_number(token)) {
    return makeint(strtol(token, NULL, 10));
  }

  return intern(token);
}

struct value_t* read(const char* str) {
  const char* strp = str;

  return readobj(&strp);
}

struct value_t* readobj_multiple(const char** strp) {
  struct value_t* res = readobj(strp);

  if (res == nil_p)
    return nil_p;

  return cons(res, readobj_multiple(strp));
}

struct value_t* read_multiple(const char* str) {
  const char* strp = str;

  struct value_t* res = readobj_multiple(&strp);
  if (res == nil_p)
    return nil_p;

  return cons(progn_p, res);
}

void concat(char** lhs, const char* rhs) {
  if (*lhs == 0) {
    *lhs = strdup(rhs);
  }
  else {
    *lhs = realloc(*lhs, strlen(*lhs) + strlen(rhs) + 1);
    strcat(*lhs, rhs);
  }
}

const char* print(struct value_t* obj) {
  char* ret = 0;

  switch(obj->type){
  case CONS:
    concat(&ret, "(");
    for (;;) {
      const char* s = print(car(obj));
      concat(&ret, s);
      free((void*)s);

      if (cdr(obj) == nil_p) {
        concat(&ret, ")");
        break;
      }

      obj = cdr(obj);

      if (obj->type != CONS) {
        concat(&ret, " . ");
        const char* s = print(obj);
        concat(&ret, s);
        free((void*)s);
        concat(&ret, ")");
        break;
      }

      concat(&ret, " ");
    }
    return ret;
  case SYMBOL:
    return strdup(obj->symbol.name);
  case INT:
    return ltoa(obj->int_value);
  case PROC:
    return strdup("#<PROC>");
  case PRIMITIVE:
    return strdup("#<PRIMITIVE>");
  case MACRO:
    return strdup("#<MACRO>");
  case GUARD:
    die("Access to deallocated memory");
    return 0;
  }
}

struct value_t* extend(struct value_t* env,
                       struct value_t* symbol,
                       struct value_t* value) {
  env->cons.car = cons(cons(symbol, value), env->cons.car);

  return env;
}

struct value_t* multiple_extend(struct value_t* env,
                                struct value_t* symbols,
                                struct value_t* values) {
  env = makeenv(env);
  struct value_t* res = env->cons.car;
  struct value_t* sym = symbols;
  struct value_t* val = values;
  for (;sym != nil_p && val != nil_p; sym = cdr(sym), val=cdr(val)) {
    res = cons(cons(car(sym), car(val)), res);
  }

  env->cons.car = res;
  return env;
}

struct value_t* find_in_env(struct value_t* symbol,
                            struct value_t* env) {
  if (symbol == nil_p || env == nil_p)
    return nil_p;

  struct value_t* entry;

  for (entry = env->cons.car; !is_nil(entry); entry = cdr(entry)) {
    if (car(car(entry)) == symbol)
      return car(entry);
  }

  return find_in_env(symbol, cdr(env));
}

struct value_t* eval(struct value_t* val, struct value_t* env);


struct value_t* eval_list(struct value_t* val, struct value_t* env) {
  if (val == nil_p)
    return nil_p;

  struct value_t * head = eval(car(val), env);
  gc_root_push(head);

  struct value_t* res = cons(head,
                             eval_list(cdr(val), env));

  gc_root_pop();

  return res;
}

struct value_t* eval_cons(struct value_t* val, struct value_t* env) {
  if (car(val) == if_p) {
    struct value_t* condition = cdr(val);
    struct value_t* action = cdr(cdr(val));
    struct value_t* alternative = cdr(cdr(cdr(val)));

    if (eval(car(condition), env) != nil_p)
      return eval(car(action), env);
    else if (alternative != nil_p)
      return eval(car(alternative), env);

    return nil_p;
  }

  if (car(val) == quote_p) {
    return car(cdr(val));
  }

  if (car(val) == setf_p) {
    struct value_t* sym = car(cdr(val));
    struct value_t* symval = car(cdr(cdr(val)));

    if (sym == nil_p || sym->type != SYMBOL)
      die("setf expects a symbol");

    struct value_t* tmp = find_in_env(sym, env);
    if (tmp == nil_p)
      die("Unbound symbol: %s\n", val->symbol.name);

    tmp->cons.cdr = symval;

    return symval;
  }

  if (car(val) == define_p) {
    struct value_t* sym = car(cdr(val));
    struct value_t* symval = eval(car(cdr(cdr(val))), env);

    if (sym == nil_p || sym->type != SYMBOL)
      die("define expects a symbol");

    extend(env, sym, symval);

    return symval;
  }

  if (car(val) == defmacro_p) {
    struct value_t* sym = car(cdr(val));
    struct value_t* params = car(cdr(cdr(val)));
    struct value_t* body = cdr(cdr(cdr(val)));

    struct value_t* macro = makemacro(params, body, toplevel_env);

    if (sym == nil_p || sym->type != SYMBOL)
      die("define expects a symbol");

    extend(toplevel_env, sym, macro);

    return macro;
  }

  if (car(val) == progn_p) {
    struct value_t* tmp = cdr(val);
    for (;;) {
      struct value_t* res = eval(car(tmp), env);
      if (cdr(tmp) == nil_p)
        return res;
      tmp=cdr(tmp);
    }
    return nil_p;
  }

  if (car(val) == lambda_p) {
    return makeproc(car(cdr(val)), cdr(cdr(val)), env);
  }

  struct value_t* proc = eval(car(val), env);

  if (proc->type == PRIMITIVE) {
    struct value_t* params = eval_list(cdr(val), env);

    return proc->primitive_op(params);
  }

  if (proc->type == PROC) {
    struct value_t* params = eval_list(cdr(val), env);
    struct value_t* new_env = multiple_extend(env,
                                              proc->proc.params,
                                              params);
    struct value_t* progn = cons(progn_p, proc->proc.body);
    gc_root_push(params);
    gc_root_push(new_env);
    gc_root_push(progn);

    struct value_t* res = eval(progn,
                               new_env);
    gc_root_pop();
    gc_root_pop();
    gc_root_pop();
    return res;
  }

  if (proc->type == MACRO) {
    struct value_t* params = cdr(val);
    struct value_t* new_env = multiple_extend(env,
                                              proc->proc.params,
                                              params);
    struct value_t* progn = cons(progn_p, proc->proc.body);

    gc_root_push(params);
    gc_root_push(new_env);
    gc_root_push(progn);

    struct value_t* new_form = eval(progn,
                                    new_env);
    gc_root_push(new_form);

    struct value_t* res = eval(new_form,
                               env);
    gc_root_pop();
    gc_root_pop();
    gc_root_pop();
    gc_root_pop();

    return res;
  }

  die("Unsupported procedure type");
  return nil_p;
}

struct value_t* eval(struct value_t* val, struct value_t* env) {
  if (val == nil_p)
    return nil_p;

  if (need_gc()) {
    collectgarbage();
  }

  struct value_t* tmp;
  switch(val->type) {
  case INT:
    return val;
  case SYMBOL:
    tmp = find_in_env(val, env);
    if (tmp == nil_p)
      die("Unbound symbol: %s\n", val->symbol.name);
    return cdr(tmp);
  case PRIMITIVE:
    return val;
  case PROC:
    return val;
  case MACRO:
    return val;
  case CONS:
    return eval_cons(val, env);
  case GUARD:
    die("Access to deallocated memory");
    return nil_p;
  };
}

struct value_t* primitive_cons(struct value_t* val) {
  return cons(car(val), car(cdr(val)));
}

struct value_t* primitive_car(struct value_t* val) {
  return car(car(val));
}

struct value_t* primitive_cdr(struct value_t* val) {
  return cdr(car(val));
}

struct value_t* primitive_plus(struct value_t* val) {
  long sum = 0;

  for (;val!=nil_p; val=cdr(val)) {
    if (car(val)->type != INT)
      die("Can't add non-integer values");

    sum = sum + car(val)->int_value;
  }

  return makeint(sum);
}

struct value_t* primitive_minus(struct value_t* val) {
  long sum = 0;
  size_t count = 0;

  for (;val!=nil_p; val=cdr(val), count=count+1) {
    if (car(val)->type != INT)
      die("Can't add non-integer values");

    if (count == 0)
      sum = sum + car(val)->int_value;
    else
      sum = sum - car(val)->int_value;
  }

  if (count == 1)
    return makeint(-sum);

  return makeint(sum);
}

struct value_t* primitive_mul(struct value_t* val) {
  long mul = 1;

  for (;val!=nil_p; val=cdr(val)) {
    if (car(val)->type != INT)
      die("Can't multiply non-integer values");

    mul = mul * car(val)->int_value;
  }

  return makeint(mul);
}

struct value_t* primitive_div(struct value_t* val) {
  if (val == nil_p)
    die("Need at least 1 integer to compare");
  if (car(val)->type != INT)
    die("Can't add non-integer values");

  long res = car(val)->int_value;

  for (val=cdr(val); val!=nil_p; val=cdr(val)) {
    if (car(val)->type != INT)
      die("Can't divide non-integer values");

    res = res / car(val)->int_value;
  }

  return makeint(res);
}


struct value_t* primitive_equals(struct value_t* val) {
  if (val == nil_p)
    die("Need at least 1 integer to compare");
  if (car(val)->type != INT)
    die("Can't add non-integer values");

  long res = car(val)->int_value;

  for (;val!=nil_p; val=cdr(val)) {
    if (car(val)->type != INT)
      die("Can't compare non-integer values");

    if (res != car(val)->int_value)
      return nil_p;
  }

  return t_p;
}


void init_env() {
  nil_p = slab_alloc();
  nil_p->type = SYMBOL;
  nil_p->symbol.name = "nil";
  symbols = cons(nil_p, nil_p);

  REGISTER_SYMBOL(t);
  REGISTER_SYMBOL(quote);
  REGISTER_SYMBOL(if);
  REGISTER_SYMBOL(lambda);
  REGISTER_SYMBOL(progn);
  REGISTER_SYMBOL(setf);
  REGISTER_SYMBOL(define);
  REGISTER_SYMBOL(defmacro);

  toplevel_env = cons(nil_p, nil_p);

  extend(toplevel_env, intern("nil"), nil_p);
  extend(toplevel_env, intern("t"), t_p);

  extend(toplevel_env, intern("cons"), makeprimitive(primitive_cons));
  extend(toplevel_env, intern("car"), makeprimitive(primitive_car));
  extend(toplevel_env, intern("cdr"), makeprimitive(primitive_cdr));
  extend(toplevel_env, intern("+"), makeprimitive(primitive_plus));
  extend(toplevel_env, intern("-"), makeprimitive(primitive_minus));
  extend(toplevel_env, intern("="), makeprimitive(primitive_equals));
  extend(toplevel_env, intern("*"), makeprimitive(primitive_mul));
  extend(toplevel_env, intern("/"), makeprimitive(primitive_div));
}


const char* read_file(const char* filename) {
  FILE *f = fopen(filename, "rb");
  fseek(f, 0, SEEK_END);
  long fsize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *string = malloc(fsize + 1);
  fread(string, 1, fsize, f);
  fclose(f);

  string[fsize] = 0;

  return string;
}

int main(int argc, char** argv) {
  init_env();

  //struct value_t* v = slab_alloc();
  //slab_free(v);

  const char* filename = 0;
  int verbose = 0;

  for (int i = 1; i<argc; i++) {
    if (strcmp(argv[i], "-v") == 0)
      verbose = 1;
    else
      filename = argv[i];
  }

  if (filename == 0)
    die("Usage: lisp [-v] <filename>\n");


  const char* str = read_file(filename);
  struct value_t* val = read_multiple(str);
  free((void*)str);
  toplevel_code = val;

  gc_root_push(val);
  gc_root_push(toplevel_env);
  gc_root_push(symbols);

  val = eval(val, toplevel_env);

  const char* res = print(val);
  printf("%s\n", res);
  free((void*)res);

  collectgarbage();

  gc_root_pop();
  gc_root_pop();
  gc_root_pop();

  if (verbose) {
    printf("memory allocations: %ld\n", number_of_allocations);
    printf("memory used: %ld\n", memory_used());

  }

  return 0;
}
