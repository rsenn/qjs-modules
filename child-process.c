#include "child-process.h"
#include "utils.h"
#include "property-enumeration.h"

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

ChildProcess*
child_process_new(JSContext* ctx) {
  return js_mallocz(ctx, sizeof(ChildProcess));
}

char**
child_process_environment(JSContext* ctx, JSValueConst object) {
  PropertyEnumeration propenum;
  Vector args;

  if(property_enumeration_init(&propenum, ctx, object, PROPENUM_DEFAULT_FLAGS))
    return 0;

  vector_init(&args, ctx);

  do {
    char* var;
    const char *name, *value;
    size_t namelen, valuelen;

    name = property_enumeration_keystrlen(&propenum, &namelen, ctx);
    value = property_enumeration_valuestrlen(&propenum, &valuelen, ctx);

    var = js_malloc(ctx, namelen + 1 + valuelen + 1);

    memcpy(var, name, namelen);
    var[namelen] = '=';
    memcpy(&var[namelen + 1], value, valuelen);
    var[namelen + 1 + valuelen] = '\0';

    JS_FreeCString(ctx, name);
    JS_FreeCString(ctx, value);

    vector_push(&args, var);

  } while(property_enumeration_next(&propenum));

  vector_emplace(&args, sizeof(char*));
  return (char**)args.data;
}

int
child_process_spawn(ChildProcess* cp) {
  int pid;
  if((pid = fork()) == 0) {

    if(cp->child_fds) {
      for(int i = 0; i < cp->num_fds; i++) {
        if(cp->child_fds[i] >= 0)
          dup2(cp->child_fds[i], i);
      }
    }

    if(cp->cwd)
      chdir(cp->cwd);

    setuid(cp->uid);
    setgid(cp->gid);

    execvp(cp->file, cp->args, cp->env);
    perror("execvp()");
    exit(errno);
  }

  return cp->pid = pid;
}
