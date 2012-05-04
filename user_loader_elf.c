
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <elf.h>
#include <link.h>

#include "minimal_libc.h"
#include "shared.h"


struct elf_obj {
  struct dynnacl_obj *dynnacl_obj;
  ElfW(Sym) *dt_symtab;
  char *dt_strtab;
  uint32_t *dt_hash;

  uintptr_t tls_base_offset;
};

struct tls_index {
  void *module;
  uintptr_t offset;
};

static void allocate_tls(struct elf_obj *elf_obj) {
  if (elf_obj->tls_base_offset != 0)
    return;
  printf("allocating tls\n");
  void *tls_template;
  size_t file_size;
  size_t mem_size;
  elf_get_tls_template(elf_obj->dynnacl_obj, &tls_template,
                       &file_size, &mem_size);
  elf_obj->tls_base_offset = tls_allocate_static_tls(mem_size);
  char *base = tls_get_base() + elf_obj->tls_base_offset;
  /* Copy the TLS template.  This must happen only after relocations
     are applied. */
  memcpy(base, tls_template, file_size);
  memset(base + file_size, 0, mem_size - file_size);
}

/* We need the regparm attribute for x86-32. */
__attribute__((regparm(1)))
static void *tls_get_addr(struct tls_index *info) {
  printf("tls_get_addr(%i: %i, %i) called\n", info, info->module, info->offset);
  struct elf_obj *elf_obj = info->module;
  allocate_tls(elf_obj);
  return tls_get_base() + elf_obj->tls_base_offset + info->offset;
}

static unsigned long elf_hash(const uint8_t *name) {
  unsigned long h = 0;
  unsigned long g;
  while (*name != 0) {
    h = (h << 4) + *name++;
    g = h & 0xf0000000;
    if (g != 0)
      h ^= g >> 24;
    h &= ~g;
  }
  return h;
}

static void *get_biased_dynamic_entry(struct dynnacl_obj *obj, int type) {
  uintptr_t val = elf_get_dynamic_entry(obj, type);
  assert(val != 0);
  return (void *) (elf_get_load_bias(obj) + val);
}

static void dump_symbols(struct elf_obj *obj) {
  uint32_t nchain = obj->dt_hash[1];
  uint32_t index;
  for (index = 0; index < nchain; index++) {
    ElfW(Sym) *sym = &obj->dt_symtab[index];
    printf("symbol %i: %s\n", index, obj->dt_strtab + sym->st_name);
  }
}

static ElfW(Sym) *look_up_symbol(struct elf_obj *obj, const char *name) {
  uint32_t nbucket = obj->dt_hash[0];
  uint32_t nchain = obj->dt_hash[1];
  uint32_t *bucket = obj->dt_hash + 2;
  uint32_t *chain = bucket + nbucket;
  unsigned long entry = bucket[elf_hash((uint8_t *) name) % nbucket];
  while (entry != 0) {
    assert(entry < nchain);
    ElfW(Sym) *sym = &obj->dt_symtab[entry];
    char *name2 = obj->dt_strtab + sym->st_name;
    if (strcmp(name, name2) == 0) {
      return sym;
    }
    entry = chain[entry];
  }
  return NULL;
}

static void *look_up_func(struct elf_obj *obj, const char *name) {
  ElfW(Sym) *sym = look_up_symbol(obj, name);
  if (sym == NULL)
    return NULL;
  return (void *) (elf_get_load_bias(obj->dynnacl_obj) + sym->st_value);
}

const char *example_import0() {
  return "called imported func #0";
}

const char *example_import1() {
  return "called imported func #1";
}

int example_args_test(int arg1, int arg2, int arg3, int arg4,
                      int arg5, int arg6, int arg7, int arg8) {
  return (arg1 == 1 && arg2 == 2 && arg3 == 3 && arg4 == 4 &&
          arg5 == 5 && arg6 == 6 && arg7 == 7 && arg8 == 8);
}

static int resolver_call_count = 0;

static void *my_plt_resolver(void *handle, int import_id) {
  struct elf_obj *elf_obj = handle;

  int sym_number = elf_symbol_id_from_import_id(elf_obj->dynnacl_obj,
                                                import_id);
  char *name = elf_obj->dt_strtab + elf_obj->dt_symtab[sym_number].st_name;

  printf("elf resolver called for func #%i: \"%s\"\n", import_id, name);
  resolver_call_count++;

  void *value;
  if (strcmp(name, "import_func0") == 0) {
    value = example_import0;
  } else if (strcmp(name, "import_func1") == 0) {
    value = example_import1;
  } else if (strcmp(name, "import_args_test") == 0) {
    value = example_args_test;
  } else if (strcmp(name, "___tls_get_addr") == 0) {
    /* x86-32 version */
    value = tls_get_addr;
  } else if (strcmp(name, "__tls_get_addr") == 0) {
    /* x86-64 version */
    value = tls_get_addr;
  } else {
    assert(0);
  }
  elf_set_plt_entry(elf_obj->dynnacl_obj, import_id, value);
  return value;
}

struct elf_obj *load_elf_file(const char *filename) {
  struct dynnacl_obj *dynnacl_obj = dynnacl_load_from_elf_file(filename);

  struct elf_obj *elf_obj = malloc(sizeof(struct elf_obj));
  assert(elf_obj != NULL);

  elf_obj->dynnacl_obj = dynnacl_obj;
  elf_obj->dt_symtab = get_biased_dynamic_entry(dynnacl_obj, DT_SYMTAB);
  elf_obj->dt_strtab = get_biased_dynamic_entry(dynnacl_obj, DT_STRTAB);
  elf_obj->dt_hash = get_biased_dynamic_entry(dynnacl_obj, DT_HASH);
  return elf_obj;
}

void test2() {
#if defined(__i386__)
# define LIBC "/lib32/libc.so.6"
#elif defined(__x86_64__)
# define LIBC "/lib/libc.so.6"
#endif
  printf("try loading libc\n");
  struct elf_obj *elf_obj = load_elf_file(LIBC);
  elf_set_plt_resolver(elf_obj->dynnacl_obj, my_plt_resolver, &elf_obj);

  int (*func)();
  func = look_up_func(elf_obj, "write");
  assert(func != NULL);
  const char *str = "called via libc!\n";
  int written = func(1, str, strlen(str));
  assert(written == strlen(str));
}

int main() {
  struct dynnacl_obj *dynnacl_obj =
    dynnacl_load_from_elf_file("example_lib_elf.so");

  struct elf_obj elf_obj;

  elf_obj.dynnacl_obj = dynnacl_obj;
  elf_obj.dt_symtab = get_biased_dynamic_entry(dynnacl_obj, DT_SYMTAB);
  elf_obj.dt_strtab = get_biased_dynamic_entry(dynnacl_obj, DT_STRTAB);
  elf_obj.dt_hash = get_biased_dynamic_entry(dynnacl_obj, DT_HASH);
  elf_obj.tls_base_offset = 0;

  dump_symbols(&elf_obj);

  printf("testing exported functions...\n");
  const char *(*func)(void);
  const char *result;

  func = look_up_func(&elf_obj, "foo");
  assert(func != NULL);
  result = func();
  assert(strcmp(result, "example string") == 0);

  func = look_up_func(&elf_obj, "bar");
  assert(func != NULL);
  result = func();
  assert(strcmp(result, "another example string") == 0);

  void *sym = look_up_func(&elf_obj, "some_undefined_sym");
  assert(sym == NULL);

  printf("testing imported functions...\n");
  elf_set_plt_resolver(dynnacl_obj, my_plt_resolver, &elf_obj);

  func = look_up_func(&elf_obj, "test_import0");
  assert(func != NULL);
  result = func();
  assert(strcmp(result, "called imported func #0") == 0);
  assert(resolver_call_count == 1);
  resolver_call_count = 0;
  result = func();
  assert(strcmp(result, "called imported func #0") == 0);
  assert(resolver_call_count == 0);

  func = look_up_func(&elf_obj, "test_import1");
  assert(func != NULL);
  result = func();
  assert(strcmp(result, "called imported func #1") == 0);
  assert(resolver_call_count == 1);
  resolver_call_count = 0;
  result = func();
  assert(strcmp(result, "called imported func #1") == 0);
  assert(resolver_call_count == 0);

  int (*func2)(void);
  func2 = look_up_func(&elf_obj, "test_args_via_plt");
  assert(func2());

  struct dynnacl_reloc *relocs;
  size_t relocs_count;
  elf_get_relocs(dynnacl_obj, &relocs, &relocs_count);
  int index;
  for (index = 0; index < relocs_count; index++) {
    struct dynnacl_reloc *reloc = &relocs[index];
    ElfW(Sym) *sym = &elf_obj.dt_symtab[reloc->r_symbol];
    printf("Applying reloc %i: %s\n", index, elf_obj.dt_strtab + sym->st_name);
    uintptr_t dest = elf_get_load_bias(dynnacl_obj) + reloc->r_offset;
    switch (reloc->r_type) {
      case R_DYNNACL_PTR:
        {
          uintptr_t value = elf_get_load_bias(dynnacl_obj) + sym->st_value;
          *(uintptr_t *) dest = value;
          break;
        }
      case R_DYNNACL_TLS_DTPMOD:
        *(void **) dest = &elf_obj;
        break;
      case R_DYNNACL_TLS_DTPOFF:
        *(uintptr_t *) dest = sym->st_value;
        break;
      case R_DYNNACL_TLS_TPOFF:
        allocate_tls(&elf_obj);
        *(uintptr_t *) dest = elf_obj.tls_base_offset + sym->st_value;
        break;
      default:
        assert(0);
        break;
    }
  }

  int (*test_var_func)(void);
  test_var_func = look_up_func(&elf_obj, "test_var");
  assert(test_var_func() == 123);
  test_var_func = look_up_func(&elf_obj, "test_var2");
  assert(test_var_func() == 456);

  test_var_func = look_up_func(&elf_obj, "test_tls_var");
  /* Only the first of these two calls will allocate the TLS area. */
  assert(test_var_func() == 321);
  assert(test_var_func() == 321);

  test_var_func = look_up_func(&elf_obj, "test_tls_var_ie");
  assert(test_var_func() == 654);

  test2();

  printf("passed\n");
  return 0;
}
