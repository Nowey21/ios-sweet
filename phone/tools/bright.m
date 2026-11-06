/*
 * bright: read or set the display brightness the way the Control Center slider does
 * (BackBoardServices, private), to see what reaches the display driver without a finger.
 *   bright            print the current level (0..1)
 *   bright LEVEL      set it (0..1)
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#import <Foundation/Foundation.h>

int main(int argc, char **argv)
{
	@autoreleasepool {
		void *h = dlopen("/System/Library/PrivateFrameworks/BackBoardServices.framework/BackBoardServices", RTLD_NOW);
		if (h == NULL) {
			fprintf(stderr, "bright: %s\n", dlerror());
			return 1;
		}
		float (*get)(void) = (float (*)(void))dlsym(h, "BKSDisplayBrightnessGetCurrent");
		void (*set)(float, int) = (void (*)(float, int))dlsym(h, "BKSDisplayBrightnessSet");
		if (get != NULL)
			printf("brightness now %.3f\n", get());
		if (argc > 1) {
			if (set == NULL) {
				fprintf(stderr, "bright: no BKSDisplayBrightnessSet\n");
				return 1;
			}
			set((float)atof(argv[1]), 1);
			[NSThread sleepForTimeInterval:1.0];
			if (get != NULL)
				printf("brightness after %.3f\n", get());
		}
	}
	return 0;
}
