#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "PKeyHash.h"
#include "parser.h"
#include "journal.h"

int *modes;
extern Flush_t* last_flush;
extern int flushes;
/* modes[tid, predicate, threads, rounds, scheduler] */
void usage(int argc, char **argv) {
	int i;
	modes = calloc(5, sizeof(int));
	modes[3] = 1; // default rounds
	modes[4] = 0; // default scheduler off
	for (i = 1 ; i < argc ; i++) {
		if (!strcmp(argv[i], "-tid") || !strcmp(argv[i], "tid") || !strcmp(argv[i], "--tid")) {
			modes[0] = 1;
		} else if (!strcmp(argv[i], "-predicate") || !strcmp(argv[i], "predicate") || !strcmp(argv[i], "--predicate")) {
			modes[1] = 1; 
		} else if (!strcmp(argv[i], "-threads") || !strcmp(argv[i], "threads") || !strcmp(argv[i], "--threads")) {
			i++;
			if (argc <= i) { goto errorInput; }
			modes[2] = atoi(argv[i]);
		} else if (!strcmp(argv[i], "-rounds") || !strcmp(argv[i], "rounds") || !strcmp(argv[i], "--rounds")) {
			i++;
			if (argc <= i) { goto errorInput; }
			int flushes = atoi(argv[i]);
			if (flushes < 1) { fprintf(stderr, "Rounds must be greater than zero\n"); exit(1); }
			modes[3] = flushes;
		} else if (!strcmp(argv[i], "-scheduler") || !strcmp(argv[i], "scheduler") || !strcmp(argv[i], "--scheduler")) {
			modes[4] = 1;
			if (modes[2] == 0) {	// if threads not given
				modes[2] = 2;	// default threads
			}
		} else {
errorInput:
			fprintf(stderr, "Wrong Input! Run like:\n%s\nor\n%s --tid\nor\n%s --tid --predicate\nor\n%s --tid --threads T\nor\n%s --threads T\nor\n%s --threads T --rounds R\nor\n%s --tid --threads T --rounds R\n"
					, argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0]);
			exit(EXIT_FAILURE);
		}
	}
}

int main(int argc, char **argv) {
	usage(argc, argv); /* modes[0]: tid on/off, modes[1]: predicate on/off*/
	MessageHead_t head;
	void *body = NULL;
	uint32_t len;
	Journal_t** journal_array = NULL;
	ValidationList_t* validation_list = validationListCreate();
	int relation_count = 0;
	while (1) {
		if (read(0, &head, sizeof(head)) <= 0) {
			exit(EXIT_FAILURE);
		} // crude error handling, should never happen

		if (head.messageLen > 0 ) {
			body = malloc(head.messageLen * sizeof(char));
			if (read(0, body, head.messageLen) <= 0) { 
				fprintf(stderr, "Error in Read Body");
				exit(EXIT_FAILURE);
			} // crude error handling, should never happen
			len -= (sizeof(head) + head.messageLen);
		}
		// And interpret it
		switch (head.type) {
			case Done:
				// validationListPrint(validation_list);
				destroySchema(journal_array, relation_count);
				validationListDestroy(validation_list);
				free(modes);
				return EXIT_SUCCESS;
			case DefineSchema:
				journal_array = processDefineSchema(body, &relation_count, modes);
				if (body != NULL)
					free(body);
				break;
			case Transaction:
				processTransaction(body,journal_array);
				if (body != NULL)
					free(body);
				break;
			case ValidationQueries:
				processValidationQueries(body,journal_array,validation_list);
				break;
			case Flush:
				processFlush(body,journal_array,validation_list);
				if(modes[3] != 1){
					while(flushes != 0){
						processFlush(last_flush,journal_array,validation_list);
					}
				}
				if (body != NULL)
					free(body);
				break;
			case Forget:
				processForget(body,journal_array,relation_count);
				if (body != NULL)
					free(body);
				break;
			default: 
				exit(EXIT_FAILURE);	// crude error handling, should never happen
		}
	}

	

	return 0;
}

