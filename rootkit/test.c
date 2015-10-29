#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/string.h>

#define DISABLE_WRITE_PROTECTION (write_cr0(read_cr0() & (~ 0x10000)))
#define ENABLE_WRITE_PROTECTION (write_cr0(read_cr0() | 0x10000))

static unsigned long **sys_call_table = (unsigned long **)0xffffffff81070a90;


asmlinkage int (*original_sys_exit)(int);

asmlinkage int our_fake_exit_function(int error_code)
{
	pr_notice("Hi from our_fake_exit\n");

        /*print message on console every time we
         *are called*/
        printk("HEY! sys_exit called with error_code=%d\n",error_code);

        /*call the original sys_exit*/
        return original_sys_exit(error_code);
}

/*this function is called when the module is
 *loaded (initialization)*/
int __init my_init(void)
{
        /*store reference to the original sys_exit*/
        original_sys_exit=(void *)0xffffffff81070a90;
	unsigned long offset;
	unsigned long** candidate;

	pr_notice("Hi from my_init\n");
	pr_notice("%p\n", sys_call_table);

	for (offset = PAGE_OFFSET; offset < ULLONG_MAX; offset += sizeof(void *)) {
		candidate = (unsigned long **) offset;
		if (*candidate == (unsigned long*)sys_call_table) {
			pr_notice("hit at candidate = %p\n", candidate);
			sys_call_table = candidate;
			pr_notice("sys_call_table = %p\n", sys_call_table);
			pr_notice("*sys_call_table = %p\n", *sys_call_table);
			break;
		}
	}

        /*manipulate sys_call_table to call our
         *fake exit function instead
         *of sys_exit*/
	pr_notice("%u", read_cr0());
	pr_notice("our_fake_exit_function = %p\n", (unsigned long*)our_fake_exit_function);
	DISABLE_WRITE_PROTECTION;
	pr_notice("%u", read_cr0());
        *sys_call_table=(unsigned long*)our_fake_exit_function;
	ENABLE_WRITE_PROTECTION;
	pr_notice("%u", (~ 0x10000));
	pr_notice("%p\n", our_fake_exit_function);
	return 0;
}


/*this function is called when the module is
 *unloaded*/
void __exit my_exit(void)
{

        /*make __NR_exit point to the original
         *sys_exit when our module
         *is unloaded*/
	DISABLE_WRITE_PROTECTION;
        *sys_call_table=(unsigned long*)original_sys_exit;
	ENABLE_WRITE_PROTECTION;
}

module_init(my_init);
module_exit(my_exit);
