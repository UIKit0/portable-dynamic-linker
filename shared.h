
#ifndef SHARED_H_
#define SHARED_H_

#include <stdint.h>

struct dynnacl_prog_header;

typedef void *(*user_plt_resolver_t)(void *handle, int import_id);

struct dynnacl_prog_header {
  void **plt_trampoline;
  void **plt_handle;
  void **pltgot;
  void *user_info;
};

/* PLT entries assume that %ebx is set up on entry on x86-32.  They
   therefore don't work as address-taken functions.  The address-taken
   version should be read from the GOT instead. */

extern void *pltgot_imports[];

#if defined(__i386__)

# define DYNNACL_PLT_BEGIN \
  asm(".pushsection \".text\",\"ax\",@progbits\n" \
      "slowpath_common:\n" \
      "push plt_handle@GOTOFF(%ebx)\n" \
      "jmp *plt_trampoline@GOTOFF(%ebx)\n" \
      ".popsection\n" \
      /* Start of PLTGOT table. */ \
      ".pushsection \".my_pltgot\",\"aw\",@progbits\n" \
      "pltgot_imports:\n" \
      ".popsection\n");

/* These PLT entries are more compact than those generated by binutils
   because they can use the short forms of the "jmp" and "push"
   instructions. */
/* In x86-32 ELF, the argument pushed by the slow path is an offset (a
   multiple of 8) rather than an index.  But in x86-64 ELF, the
   argument pushed is an index. */
# define DYNNACL_PLT_ENTRY(number, name) \
  asm(".pushsection \".text\",\"ax\",@progbits\n" \
      #name ":\n" \
      "jmp *pltgot_" #name "@GOTOFF(%ebx)\n" \
      "slowpath_" #name ":\n" \
      "push $8 * " #number "\n" \
      "jmp slowpath_common\n" \
      ".popsection\n" \
      /* Entry in PLTGOT table */ \
      ".pushsection \".my_pltgot\",\"aw\",@progbits\n" \
      "pltgot_" #name ":\n" \
      ".long slowpath_" #name "\n" \
      ".popsection\n");

#elif defined(__x86_64__)

# define DYNNACL_PLT_BEGIN \
  asm(".pushsection \".text\",\"ax\",@progbits\n" \
      "slowpath_common:\n" \
      "pushq plt_handle(%rip)\n" \
      "jmp *plt_trampoline(%rip)\n" \
      ".popsection\n" \
      /* Start of PLTGOT table. */ \
      ".pushsection \".my_pltgot\",\"aw\",@progbits\n" \
      "pltgot_imports:\n" \
      ".popsection\n");

# define DYNNACL_PLT_ENTRY(number, name) \
  asm(".pushsection \".text\",\"ax\",@progbits\n" \
      #name ":\n" \
      "jmp *pltgot_" #name "(%rip)\n" \
      "slowpath_" #name ":\n" \
      "pushq $" #number "\n" \
      "jmp slowpath_common\n" \
      ".popsection\n" \
      /* Entry in PLTGOT table */ \
      ".pushsection \".my_pltgot\",\"aw\",@progbits\n" \
      "pltgot_" #name ":\n" \
      ".quad slowpath_" #name "\n" \
      ".popsection\n");

#else
# error Unsupported architecture
#endif

#define DYNNACL_DEFINE_HEADER(user_info_value) \
  void *plt_trampoline; \
  void *plt_handle; \
  struct dynnacl_prog_header prog_header = { \
    .plt_trampoline = &plt_trampoline, \
    .plt_handle = &plt_handle, \
    .pltgot = pltgot_imports, \
    .user_info = user_info_value, \
  };

struct dynnacl_obj;

struct dynnacl_obj *dynnacl_load_from_elf_file(const char *filename);

void *dynnacl_get_user_root(struct dynnacl_obj *dynnacl_obj);
void dynnacl_set_plt_resolver(struct dynnacl_obj *dynnacl_obj,
                              user_plt_resolver_t plt_resolver,
                              void *handle);
void dynnacl_set_plt_entry(struct dynnacl_obj *dynnacl_obj,
                           int import_id, void *func);

uintptr_t elf_get_load_bias(struct dynnacl_obj *dynnacl_obj);
uintptr_t elf_get_dynamic_entry(struct dynnacl_obj *dynnacl_obj, int type);
void elf_set_plt_resolver(struct dynnacl_obj *dynnacl_obj,
                          user_plt_resolver_t plt_resolver,
                          void *handle);
void elf_set_plt_entry(struct dynnacl_obj *dynnacl_obj,
                       int import_id, void *func);
int elf_symbol_id_from_import_id(struct dynnacl_obj *dynnacl_obj,
                                 int import_id);

#endif
