#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

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

struct value_t {
  enum type_t type;

  union {
    struct cons_t cons;
    struct symbol_t symbol;
    long int_value;
    primitive_op_t primitive_op;
  };
};



struct value_t nil_v = {.type=SYMBOL, .symbol.name = "nil"};
struct value_t* nil = &nil_v;

struct value_t t_v = {.type=SYMBOL, .symbol.name = "t"};
struct value_t* t = &t_v;

struct value_t quote_v = {.type=SYMBOL, .symbol.name = "quote"};
struct value_t* quote = &quote_v;

struct value_t *symbols = &nil_v;


#define TOKEN_BUF_SIZE 256
char token_buf[TOKEN_BUF_SIZE];
size_t token_buf_used = 0;



struct value_t *cons(struct value_t* car, struct value_t* cdr) {
  struct value_t *ret = malloc(sizeof(struct value_t));

  *ret = (struct value_t){.type = CONS, .cons.car = car, .cons.cdr = cdr};

  return ret;
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

int is_nil(struct value_t* val) {
  return val == nil;
}

struct value_t *car(struct value_t* val) {
  return val->cons.car;
}

struct value_t *cdr(struct value_t* val) {
  return val->cons.cdr;
}

struct value_t* find_symbol(const char* name) {
  struct value_t* sym;
  for (sym = symbols; !is_nil(sym); sym = cdr(sym)) {
    if (strcmp(name, car(sym)->symbol.name) == 0)
      return sym;
  }
  return nil;
}

struct value_t* intern(const char* name) {
  struct value_t* sym = find_symbol(name);

  if (sym != nil)
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

  if (strcmp(token, ")") == 0) {
    return nil;
  }

  *strp = saved;

  struct value_t* head = readobj(strp);
  struct value_t* tail = readlist(strp);

  return cons(head, tail);
}


struct value_t* readobj(const char** strp) {
  const char* token = gettoken(strp);

  if (strcmp(token, "(") == 0) {
    return readlist(strp);
  }

  if (strcmp(token, "\'") == 0) {
    return cons(quote, cons(readobj(strp), nil));
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

      if (cdr(obj) == nil) {
        concat(&ret, ")");
        break;
      }

      obj = cdr(obj);

      if (obj->type != CONS) {
        concat(&ret, " . ");
        const char* s = print(car(obj));
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

int main() {
  const char* str = "(+ 1 2 -34 asf '(5 6))";

  struct value_t* val = read(str);

  const char* res = print(val);

  printf("%s\n", res);

  free((void*)res);

  return 0;
}
