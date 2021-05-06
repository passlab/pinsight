#include <stdlib.h>
#include <stdio.h>

#include "tp.h"
#include "app.h"

static int array_of_ints[] = {
    100, -35, 1, 23, 14, -6, 28, 1001, -3000,
};

int main(int argc, char* argv[])
{
    FILE *stream;
    struct app_struct app_struct;


    puts("Hello, App!\nPress Enter to continue...");

    /*
     * The following getchar() call is only placed here for the purpose
     * of this demonstration, to pause the application in order for
     * you to have time to list its tracepoints. It's not needed
     * otherwise.
     */
    getchar();

    tracepoint(my_provider, simple_event, argc, argv[0]);
    stream = fopen("/tmp/app.txt", "w");

    if (!stream) {
        fprintf(stderr,
                "Error: Cannot open /tmp/app.txt for writing\n");
        return EXIT_FAILURE;
    }

    if (fprintf(stream, "0123456789") != 10) {
        fclose(stream);
        fprintf(stderr, "Error: Cannot write to /tmp/app.txt\n");
        return EXIT_FAILURE;
    }

    tracepoint(my_provider, big_event, 35, "hello tracepoint",
               stream, -3.14, array_of_ints);
    fclose(stream);
    app_struct.b = argc;
    app_struct.c = "[the string]";
    tracepoint(my_provider, event_instance1, 23, &app_struct);
    app_struct.b = argc * 5;
    app_struct.c = "[other string]";
    tracepoint(my_provider, event_instance2, 17, &app_struct);
    app_struct.b = 23;
    app_struct.c = "nothing";
    tracepoint(my_provider, event_instance3, 52, &app_struct);

    return EXIT_SUCCESS;
}