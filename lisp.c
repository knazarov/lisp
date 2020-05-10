#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

enum type_t {
    SYMBOL,
    CONS,
    INT,
    PROC,
    PRIMITIVE,
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

struct env_t {
  struct value_t* env;
  struct env_t* parent;
};

struct value_t {
  enum type_t type;

  union {
    struct cons_t cons;
    struct symbol_t symbol;
    struct proc_t proc;
    long int_value;
    primitive_op_t primitive_op;
  };
};


#define DEFSYM(symname) \
  struct value_t symname##_v = {.type=SYMBOL, .symbol.name = #symname }; \
  struct value_t* symname##_p = &symname##_v;

#define REGISTER_SYMBOL(symname) \
  symbols = cons(symname##_p, symbols);

#define REGISTER_PRIMITIVE(symname, fun)  \
  toplevel_env = extend(toplevel_env, \
                        intern(symname), \
                        makeprimitive(fun));


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

struct value_t *symbols = &nil_v;
struct value_t *toplevel_env = &nil_v;


#define TOKEN_BUF_SIZE 256
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


struct value_t *cons(struct value_t* car, struct value_t* cdr) {
  struct value_t *ret = malloc(sizeof(struct value_t));

  *ret = (struct value_t){.type = CONS, .cons.car = car, .cons.cdr = cdr};

  return ret;
}

int is_nil(struct value_t* val) {
  return val == nil_p;
}

struct value_t *car(struct value_t* val) {
  if (val == nil_p)
    return nil_p;

  return val->cons.car;
}

struct value_t *cdr(struct value_t* val) {
  if (val == nil_p)
    return nil_p;

  return val->cons.cdr;
}

struct value_t* makeint(long val) {
  struct value_t *ret = malloc(sizeof(struct value_t));
  *ret = (struct value_t){.type = INT, .int_value = val};

  return ret;
}

struct value_t* makesym(const char* name) {
  struct value_t *ret = malloc(sizeof(struct value_t));
  *ret = (struct value_t){.type = SYMBOL, .symbol.name = strdup(name)};

  return ret;
}

struct value_t* makeprimitive(primitive_op_t op) {
  struct value_t *ret = malloc(sizeof(struct value_t));
  *ret = (struct value_t){.type = PRIMITIVE, .primitive_op = op};

  return ret;
}

struct value_t* makeproc(struct value_t* params,
                         struct value_t* body,
                         struct value_t * env) {
  struct value_t *ret = malloc(sizeof(struct value_t));
  *ret = (struct value_t){.type = PROC,
                          .proc.params = params,
                          .proc.body = body,
                          .proc.env = env};

  return ret;
}


struct value_t* find_symbol(const char* name) {
  struct value_t* sym;
  for (sym = symbols; !is_nil(sym); sym = cdr(sym)) {
    if (strcmp(name, car(sym)->symbol.name) == 0)
      return car(sym);
  }
  return nil_p;
}

struct value_t* intern(const char* name) {
  struct value_t* sym = find_symbol(name);

  if (sym != nil_p)
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
    if (*p == '\0') {
      return 0;
    }

    if (isspace(*p))
      ++p;
  } while(isspace(*p));

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
    asprintf(&ret, "%li", obj->int_value);
    return ret;
  case PROC:
    return strdup("#<PROC>");
  case PRIMITIVE:
    return strdup("#<PRIMITIVE>");
  }
}

struct value_t* extend(struct value_t* env,
                       struct value_t* symbol,
                       struct value_t* value) {

  return cons(cons(symbol, value),
              env);
}

struct value_t* multiple_extend(struct value_t* env,
                                struct value_t* symbols,
                                struct value_t* values) {
  struct value_t* res = env;
  struct value_t* sym = symbols;
  struct value_t* val = values;
  for (;sym != nil_p && val != nil_p; sym = cdr(sym), val=cdr(val)) {
    res = cons(cons(car(sym), car(val)), res);
  }

  return res;
}

struct value_t* assoc(struct value_t* symbol, struct value_t* alist) {
  if (symbol == nil_p || alist == nil_p)
    return nil_p;


  struct value_t* entry;
  for (entry = alist; !is_nil(entry); entry = cdr(entry)) {
    if (car(car(entry)) == symbol)
      return car(entry);
  }

  return nil_p;
}


struct value_t* eval(struct value_t* val, struct value_t* env);


struct value_t* eval_list(struct value_t* val, struct value_t* env) {
  if (val == nil_p)
    return nil_p;

  return cons(eval(car(val), env),
              eval_list(cdr(val), env));
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

    struct value_t* tmp = assoc(sym, env);
    if (tmp == nil_p)
      die("Unbound symbol: %s\n", val->symbol.name);

    tmp->cons.cdr = symval;

    return symval;
  }

  if (car(val) == define_p) {
    struct value_t* sym = car(cdr(val));
    struct value_t* symval = car(cdr(cdr(val)));

    if (sym == nil_p || sym->type != SYMBOL)
      die("define expects a symbol");

    toplevel_env->cons.cdr = cons(cons(sym, symval),
                                  cdr(toplevel_env));
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
  struct value_t* params = eval_list(cdr(val), env);

  if (proc->type == PRIMITIVE) {
    return proc->primitive_op(params);
  }

  if (proc->type == PROC) {
    struct value_t* new_env = multiple_extend(env,
                                              proc->proc.params,
                                              params);

    return eval(cons(progn_p, proc->proc.body),
                new_env);
  }

  die("Unsupported procedure type");
  return nil_p;
}

struct value_t* eval(struct value_t* val, struct value_t* env) {
  if (val == nil_p)
    return nil_p;

  struct value_t* tmp;
  switch(val->type) {
  case INT:
    return val;
  case SYMBOL:
    tmp = assoc(val, env);
    if (tmp == nil_p)
      die("Unbound symbol: %s\n", val->symbol.name);
    return cdr(tmp);
  case PRIMITIVE:
    return val;
  case PROC:
    return val;
  case CONS:
    return eval_cons(val, env);
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


void init_env() {
  REGISTER_SYMBOL(nil);
  REGISTER_SYMBOL(t);
  REGISTER_SYMBOL(quote);
  REGISTER_SYMBOL(if);
  REGISTER_SYMBOL(lambda);
  REGISTER_SYMBOL(progn);
  REGISTER_SYMBOL(setf);
  REGISTER_SYMBOL(define);

  REGISTER_PRIMITIVE("cons", primitive_cons);
  REGISTER_PRIMITIVE("car", primitive_car);
  REGISTER_PRIMITIVE("cdr" ,primitive_cdr);
  REGISTER_PRIMITIVE("+" ,primitive_plus);
  REGISTER_PRIMITIVE("-" ,primitive_minus);
}


int main() {
  init_env();

  const char* str = "((lambda (x) (setf x 3) (+ x 1)) 2) nil";
  struct value_t* val = read_multiple(str);

  const char* res = print(val);
  printf("parsed code: %s\n", res);
  free((void*)res);

  val = eval(val, toplevel_env);

  res = print(val);
  printf("result: %s\n", res);
  free((void*)res);

  return 0;
}
