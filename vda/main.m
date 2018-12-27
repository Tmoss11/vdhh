//
//  main.m
//  vmx
//
//  Created by Boris Remizov on 29/08/16.
//  Copyright © 2016 Veertu Labs Ltd. All rights reserved.
//

#import "window/VmApp.h"
#import "cocoa_util.h"
#import <vlaunch/vsystem.h>
#import <VMManager/vmlibrary_ops.h>
#import <Cocoa/Cocoa.h>

extern bool use_hdpi;
extern int gArgc;
extern char **gArgv;
extern VmAppController *appController;

#ifdef DEBUG
// intercept exit() calls from other code
void exit(int exitCode) __dead2 {
    assert(EXIT_SUCCESS == exitCode);
    _exit(exitCode);
}
#endif

static NSString* _vm_name = nil;

const char *get_vm_folder()
{
    return [_vm_name fileSystemRepresentation];
}

extern int _argc;
extern char **_argv;
extern char **_env;
extern void *veertu_headless_thread(void *p);

#ifdef DEBUG
int _veertu_loglevel = 999;
#else
int _veertu_loglevel = 0;
#endif

int main (int argc, const char * argv[]) {

    @autoreleasepool {
        gArgc = argc;
        gArgv = (char **)argv;
        int i;

        const char *vm_name;
        for (i = 1; i < argc; i++) {
            const char *opt = argv[i];

            if (opt[0] == '-') {
                /* Treat --foo the same as -foo.  */
                if (opt[1] == '-') {
                    opt++;
                }
                if (!strcmp(opt, "-vm") && i < argc - 1) {
                    ++i;
                    vm_name = argv[i];
                    _vm_name = [NSString stringWithUTF8String:vm_name];
                } else if (!strcmp(opt, "-vlaunch_rd") && i < argc - 1) {
                    ++i;
                    int fd = -1;
                    if (argv[0] == '/' || argv[0] == '\\') {
                        // check the argument is file
                        fd = open(argv[i], O_RDONLY);
                        fcntl(fd, F_SETFD, FD_CLOEXEC);
                    } else {
                        // inherited file descriptor
                        fd = strtol(argv[i], NULL, 0);
                    }
                    vlaunchfd[0] = fd;
                } else if (!strcmp(opt, "-vlaunch_wr") && i < argc - 1) {
                    ++i;
                    int fd = -1;
                    if (argv[0] == '/' || argv[0] == '\\') {
                        // check the argument is file
                        fd = open(argv[i], O_WRONLY);
                        fcntl(fd, F_SETFD, FD_CLOEXEC);
                    } else {
                        // inherited file descriptor
                        fd = strtol(argv[i], NULL, 0);
                    }
                    vlaunchfd[1] = fd;
                } else if (!strcmp(opt, "-loglevel") && i < argc - 1) {
                    ++i;
                    _veertu_loglevel = strtol(argv[i], NULL, 0);
                }
            }
        }
        struct VMAddOnsSettings settings = {0};
        if (get_addons_settings(get_vm_folder(), &settings) && settings.hdpi)
            use_hdpi = settings.hdpi;

//        [VmApp sharedApplication];

        // Create an Application controller
        VM* vm = [[VMLibrary sharedVMLibrary] readVmProperties:[NSString stringWithUTF8String:get_vm_folder()]];
        if (vm.advanced.headless) {
            _argc = argc;
            _argv = argv;
            //_env = *_NSGetEnviron();
            pthread_t tid;
            pthread_create(&tid, NULL, veertu_headless_thread, CFBridgingRetain(vm));

            register_power_events();

            CFRunLoopRun();
        } else {
            // Pull this console process up to being a fully-fledged graphical
            // app with a menubar and Dock icon
            [NSApplication sharedApplication].activationPolicy = NSApplicationActivationPolicyRegular;

            appController = [[VmAppController alloc] initWithVM:vm];
            [NSApplication sharedApplication].delegate = appController;

            // Start the main event loop
            [[NSApplication sharedApplication] run];
        }
    }
    
    return 0;
}
