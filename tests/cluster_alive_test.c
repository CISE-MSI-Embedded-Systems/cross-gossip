#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

#define NODES 3

int main(int argc, char **argv) {
  const char *node = argv[1];

  pid_t pids[NODES];

  for (int i = 0; i < NODES; i++) {
    pid_t pid = fork();

    if (pid == 0) {
      char id[16];
      snprintf(id, sizeof(id), "%d", i + 1);

      execl(node, node, id, NULL);
      exit(1);
    }

    pids[i] = pid;
  }

  // sleep(8);

  for (int i = 0; i < NODES; i++)
    kill(pids[i], SIGTERM);

  for (int i = 0; i < NODES; i++)
    waitpid(pids[i], NULL, 0);

  return 0;
}
