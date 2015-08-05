#include <rtems/imfs.h>
#include <rtems/malloc.h>
#include <rtems/libcsupport.h>

#include <sys/stat.h>

#include <net/if.h>

#include <assert.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>


#include <machine/rtems-bsd-commands.h>

#include <rtems.h>
#include <rtems/stackchk.h>
#include <rtems/bsd/bsd.h>



#include <bsp.h>

#if defined(LIBBSP_ARM_ALTERA_CYCLONE_V_BSP_H)
  #define NET_CFG_INTERFACE_0 "dwc0"
#elif defined(LIBBSP_ARM_REALVIEW_PBX_A9_BSP_H)
  #define NET_CFG_INTERFACE_0 "smc0"
#elif defined(LIBBSP_ARM_XILINX_ZYNQ_BSP_H)
  #define NET_CFG_INTERFACE_0 "cgem0"
#elif defined(__GENMCF548X_BSP_H)
  #define NET_CFG_INTERFACE_0 "fec0"
#else
  #define NET_CFG_INTERFACE_0 "lo0"
#endif

#include "rtems_filesystem.h"

static void
default_network_set_self_prio(rtems_task_priority prio)
{
	rtems_status_code sc;

	sc = rtems_task_set_priority(RTEMS_SELF, prio, &prio);
	assert(sc == RTEMS_SUCCESSFUL);
}

static void
default_network_ifconfig_lo0(void)
{
	int exit_code;
	char *lo0[] = {
		"ifconfig",
		"lo0",
		"inet",
		"127.0.0.1",
		"netmask",
		"255.255.255.0",
		NULL
	};
	char *lo0_inet6[] = {
		"ifconfig",
		"lo0",
		"inet6",
		"::1",
		"prefixlen",
		"128",
		"alias",
		NULL
	};

	exit_code = rtems_bsd_command_ifconfig(RTEMS_BSD_ARGC(lo0), lo0);
	assert(exit_code == EX_OK);

	exit_code = rtems_bsd_command_ifconfig(RTEMS_BSD_ARGC(lo0_inet6), lo0_inet6);
	assert(exit_code == EX_OK);
}

static void
default_network_ifconfig_hwif0(char *ifname)
{
	int exit_code;
	char *ifcfg[] = {
		"ifconfig",
		ifname,
		"inet",
		NET_CFG_SELF_IP,
		"netmask",
		NET_CFG_NETMASK,
		NULL
	};

	exit_code = rtems_bsd_command_ifconfig(RTEMS_BSD_ARGC(ifcfg), ifcfg);
	assert(exit_code == EX_OK);
}

static void
default_network_route_hwif0(char *ifname)
{
	int exit_code;
	char *dflt_route[] = {
		"route",
		"add",
		"-host",
		NET_CFG_GATEWAY_IP,
		"-iface",
		ifname,
		NULL
	};
	char *dflt_route2[] = {
		"route",
		"add",
		"default",
		NET_CFG_GATEWAY_IP,
		NULL
	};

	exit_code = rtems_bsd_command_route(RTEMS_BSD_ARGC(dflt_route), dflt_route);
	assert(exit_code == EXIT_SUCCESS);

	exit_code = rtems_bsd_command_route(RTEMS_BSD_ARGC(dflt_route2), dflt_route2);
	assert(exit_code == EXIT_SUCCESS);
}

static void
default_network_on_exit(int exit_code, void *arg)
{
	rtems_stack_checker_report_usage_with_plugin(NULL,
	    rtems_printf_plugin);

	if (exit_code == 0) {
		puts("*** END OF SIMULATION ***");
	}
}

int filesystem_initialization(){
#define TARFILE_START Filesys
#define TARFILE_SIZE  Filesys_size
 printf("Untaring from memory - ");
struct stat sb;
int  sc = Untar_FromMemory((void *)TARFILE_START, TARFILE_SIZE);
  if (sc != RTEMS_SUCCESSFUL) {
    printf ("error: untar failed: %s\n", rtems_status_text (sc));
    exit(1);
  }
  printf ("successful\n");
}

static void
Init(rtems_task_argument arg)
{
	rtems_status_code sc;
	char *ifname;

	puts("*** MONKEY HTTP SERVER SIMULATION ***");

	on_exit(default_network_on_exit, NULL);

	/* Let other tasks run to complete background work */
	default_network_set_self_prio(RTEMS_MAXIMUM_PRIORITY - 1);

	rtems_bsd_initialize();

	ifname = NET_CFG_INTERFACE_0;

	/* Let the callout timer allocate its resources */
	sc = rtems_task_wake_after(2);
	assert(sc == RTEMS_SUCCESSFUL);

	default_network_ifconfig_lo0();
	default_network_ifconfig_hwif0(ifname);
	default_network_route_hwif0(ifname);
        filesystem_initialization();

	monkey_main();
}

#include <machine/rtems-bsd-sysinit.h>

SYSINIT_NEED_NET_PF_UNIX;
SYSINIT_NEED_NET_IF_LAGG;
SYSINIT_NEED_NET_IF_VLAN;

#include <bsp/nexus-devices.h>

#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_STUB_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_ZERO_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_LIBBLOCK

#define CONFIGURE_USE_IMFS_AS_BASE_FILESYSTEM

#define CONFIGURE_LIBIO_MAXIMUM_FILE_DESCRIPTORS 512

#define CONFIGURE_MAXIMUM_USER_EXTENSIONS 1

#define CONFIGURE_UNLIMITED_ALLOCATION_SIZE 32
#define CONFIGURE_UNLIMITED_OBJECTS
#define CONFIGURE_UNIFIED_WORK_AREAS

#define CONFIGURE_STACK_CHECKER_ENABLED

#define CONFIGURE_FIFOS_ENABLED
#define CONFIGURE_MAXIMUM_FIFOS 8
#define CONFIGURE_MAXIMUM_PIPES 8

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_INIT_TASK_STACK_SIZE (32 * 1024)
#define CONFIGURE_INIT_TASK_INITIAL_MODES RTEMS_DEFAULT_MODES
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT

#define CONFIGURE_INIT

#include <rtems/confdefs.h>
