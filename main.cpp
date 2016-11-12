#include "tongsengmod.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>

static bool running = false;
static bool verbose = false;
static std::string host("localhost");
static int port = 57120;
static int deviceIndex = 0; //Hopefully, 0 for internal, 1 for external

static void show_help()
{
	std::cout << "Usage: tongsengmod [options] [host] [port] [device index]" << std::endl;
	std::cout << "        -v verbose" << std::endl;
	std::cout << "        -h show help" << std::endl;
}

static void stop(int param)
{
	running = false;
}

static void init(int argc, char** argv)
{
	int aflag = 0;
	int bflag = 0;
	char *cvalue = NULL;
	int index;
	int c;

	opterr = 0;

	while ((c = getopt(argc, argv, "v")) != -1) {
		switch (c) {
			case 'v':
				verbose = true;
				break;
			case 'h':
				show_help();
				exit(0);
			default:
				show_help();
				exit(1);
		}
	}

	for (index=optind, c=0; index < argc; index++, c++) {
		switch (c) {
			case 0:
				host = argv[index];
				break;
			case 1:
				port = atoi(argv[index]);
				break;
			case 2: //last arg is device index. 0 for internal, 1 for external.
				deviceIndex = atoi(argv[index]);
				break;
			default:
				break;
		}
	}
}

int main(int argc, char** argv)
{
	init(argc, argv);
	std::cout << "Host: " << host << std::endl;
	std::cout << "Port: " << port << std::endl;
	std::cout << "Verbose: " << verbose << std::endl;
	std::cout << "(with external trackpad support)" << std::endl;

	signal(SIGINT, stop);
	signal(SIGHUP, stop);
	signal(SIGQUIT, stop);
	signal(SIGTERM, stop);

	tongseng_set_hostname_and_port(host.c_str(), port);
	tongseng_set_verbose(verbose);
	tongseng_start(deviceIndex);

	// Loop until the program is stopped.
	running = true;
	while (running) { 
		usleep(1000);
	};

	std::cout << "Cleaning up.." << std::endl;

	tongseng_stop();

	std::cout << "Tongsengmod stopped." << std::endl;

	return 0;
}


