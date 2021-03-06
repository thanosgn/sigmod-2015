#include <stdio.h>
#include <stdlib.h>
#include "journal.h"

/* List functions */

List_node *insert_start(List_t *l_info, JournalRecord_t* d) {
	List_node *n = malloc(sizeof(List_node));
	ALLOCATION_ERROR(n);
	// JournalRecord_t* new_d = copyJournalRecord(d);
	JournalRecord_t* new_d = d;
	n->data = new_d;
	n->next = l_info->list_beg;
	n->prev = NULL;
	l_info->list_beg = n;
	if (n->next != NULL)
		n->next->prev = n;
	if (l_info->list_end == NULL)
		l_info->list_end = n;
	l_info->size++;
	return n;
}

void insert_end(List_t *l_info, JournalRecord_t* d) {
	List_node *n = malloc(sizeof(List_node));
	ALLOCATION_ERROR(n);
	JournalRecord_t* new_d = d;
	n->data = new_d;
	n->next = NULL;
	n->prev = l_info->list_end;
	l_info->list_end = n;
	if (n->prev != NULL)
		n->prev->next = n;
	if (l_info->list_beg == NULL)
		l_info->list_beg = n;
	l_info->size++;
}

void remove_end(List_t *l_info) {
	List_node *n = l_info->list_end;
	if (n->prev == NULL)
		l_info->list_beg = n->next;
	else
		n->prev->next = n->next;
	if (n->next == NULL)
		l_info->list_end = n->prev;
	else
		n->next->prev = n->prev;
	l_info->size--;
	free(n);
}

List_t *info_init(void) {
	List_t *l_info = malloc(sizeof(List_t));
	ALLOCATION_ERROR(l_info);
	l_info->list_beg = NULL;
	l_info->list_end = NULL;
	l_info->size = 0;
	return l_info;
}

void destroy_list(List_t* list){
	while(!isEmpty(list)){
		remove_end(list);
	}
	free(list);
}

bool isEmpty(List_t* list){
	return (list->size == 0);
}


void printList(List_t* list){
	List_node* n = list->list_beg;
	while(n != NULL){
		printJournalRecord(n->data);
		n = n->next;
	}
	printf("\n");
}

/* Journal_t functions */

Journal_t* createJournal(uint64_t relation_id, int* modes) {
	Journal_t* journal = malloc(sizeof(Journal_t));
	ALLOCATION_ERROR(journal);
	journal->journal_capacity = JOURNAL_CAPACITY_INIT;
	journal->records = malloc(journal->journal_capacity * sizeof(JournalRecord_t));
	ALLOCATION_ERROR(journal->records);
	journal->num_of_recs = 0;
	journal->relation_id = relation_id;
	journal->index = createHash();
	journal->tid_index = (modes[0] == 1) ? tidCreateHash() : NULL;
	journal->predicate_index = (modes[1] == 1) ? predicateCreateHash() : NULL;
	return journal;
}

int increaseJournal(Journal_t* journal){
	//Double the capacity
	journal->journal_capacity *= 2;
	journal->records = realloc(journal->records, journal->journal_capacity * sizeof(JournalRecord_t));
	ALLOCATION_ERROR(journal->records);
	return 0;
}

/*Returns a pointer to the inserted record*/

void insertJournalRecord(Journal_t* journal, uint64_t transaction_id, size_t columns, const uint64_t* column_values, bool dirty_bit){
	if(journal->num_of_recs >= journal->journal_capacity) {
		increaseJournal(journal);
	}
	//Insert the record
	journal->records[journal->num_of_recs].transaction_id = transaction_id;
	journal->records[journal->num_of_recs].dirty_bit = dirty_bit;
	journal->records[journal->num_of_recs].columns = columns;
	journal->records[journal->num_of_recs].column_values = malloc(journal->records[journal->num_of_recs].columns * sizeof(uint64_t));
	ALLOCATION_ERROR(journal->records[journal->num_of_recs].column_values);
	int i;
	for(i = 0; i < journal->records[journal->num_of_recs].columns; i++){
		journal->records[journal->num_of_recs].column_values[i] = column_values[i];
	}
	RangeArray* range_array = malloc(sizeof(RangeArray));
	ALLOCATION_ERROR(range_array);
	range_array->transaction_id = transaction_id;
	range_array->rec_offset = journal->num_of_recs;
	insertHashRecord(journal->index, column_values[0], range_array);
	free(range_array);

	if(journal->tid_index != NULL){
		tidSubBucket* tid_sub_bucket = malloc(sizeof(tidSubBucket));
		ALLOCATION_ERROR(tid_sub_bucket);
		tid_sub_bucket->transaction_id = transaction_id;
		tid_sub_bucket->rec_offset = journal->num_of_recs;
		tidInsertHashRecord(journal->tid_index, tid_sub_bucket);
		free(tid_sub_bucket);
	}
	journal->num_of_recs++;
}

uint64_t getRecordCount(Journal_t* journal, uint64_t range_start, uint64_t range_end, uint64_t* first_offset){
	uint64_t first_appearance_start = 0, first_appearance_end = 0;
	if(journal->tid_index != NULL) {
		/*Search for range_start*/
		bool found1 = false, found2 = false;
		bool range_start_found = true;
		uint64_t transaction_id1 = range_start;
		uint64_t transaction_id2 = range_end;
		first_appearance_start = tidGetHashOffset(journal->tid_index, transaction_id1, &found1);
		first_appearance_end = tidGetHashOffset(journal->tid_index, transaction_id2, &found2);
		if(found1 == false){
			if(found2 == false) {
				first_appearance_start = findRangeStart(journal, 0, journal->num_of_recs-1, range_start, first_offset,&range_start_found);
				if(range_start_found == false){
					*first_offset = 0;
					return 0;
				}
				first_appearance_end = findRangeEnd(journal, 0, journal->num_of_recs-1, range_end, first_offset, first_appearance_start);
			} else {
				first_appearance_start = findRangeStart(journal, 0, first_appearance_end, range_start, first_offset, &range_start_found);
				if(range_start_found == false){
					*first_offset = 0;
					return 0;
				}
			}
		} else {
			if(found2 == false){
				first_appearance_end = findRangeEnd(journal, 0, journal->num_of_recs-1, range_end, first_offset, first_appearance_start);
			} 			
		}
	} else {
		bool found = true;
		first_appearance_start = findRangeStart(journal, 0, journal->num_of_recs-1, range_start, first_offset, &found);
		if(found == false){
			*first_offset = 0;
			return 0;
		}
		first_appearance_end = findRangeEnd(journal, 0, journal->num_of_recs-1, range_end, first_offset, first_appearance_start);
	}

	uint64_t last = first_appearance_end;
	while(last+1 < journal->num_of_recs && journal->records[last+1].transaction_id <= range_end ) {
		last++;
	}
	*first_offset = first_appearance_start;
	return (last - first_appearance_start) + 1;
}

uint64_t findRangeEnd(Journal_t* journal, uint64_t first, uint64_t last, uint64_t key, uint64_t* first_offset, uint64_t first_appearance_start){
	uint64_t middle = (first+last)/2;
	bool not_found = false;
	uint64_t first_appearance_end = 0;

	while (first <= last && not_found == false) {
		if (journal->records[middle].transaction_id < key){
			first = middle + 1;    
		}
		else if (journal->records[middle].transaction_id == key) {
			first_appearance_end = middle;
			break;
		}
		else{
			if(middle == 0){
				not_found = true;
				break;
			}
			last = middle - 1;
		}
		middle = (first + last)/2;
	}
	if (first > last || not_found == true){	//Not found
		first_appearance_end = (last > first) ? last : first;
		if(first_appearance_end >= journal->num_of_recs){
			first_appearance_end = journal->num_of_recs-1;
		}
		while(first_appearance_end >= first_appearance_start && journal->records[first_appearance_end].transaction_id > key){
			first_appearance_end--;
		}
	}
	return first_appearance_end;
}

uint64_t findRangeStart(Journal_t* journal, uint64_t first, uint64_t last, uint64_t key, uint64_t* first_offset, bool* found){
	uint64_t middle = (first+last)/2;
	bool not_found = false;
	*found = true;
	uint64_t first_appearance_start = 0;
	while (first <= last && not_found == false) {
		if (journal->records[middle].transaction_id < key){
			first = middle + 1;    
		}
		else if (journal->records[middle].transaction_id == key) {
			first_appearance_start = middle;
			break;
		}
		else{
			if(middle == 0){
				not_found = true;
				break;
			}
			last = middle - 1;
		}
		middle = (first + last)/2;
	}
	if (first > last || not_found == true){	//Not found
		first_appearance_start = (last <= first) ? last : first;
		while(first_appearance_start < journal->num_of_recs && journal->records[first_appearance_start].transaction_id < key){
			first_appearance_start++;
		}
	}
	if(first_appearance_start >= journal->num_of_recs){
		*first_offset = 0;
		*found = false;
		return 0;
	}
	while(first_appearance_start > 0 && journal->records[first_appearance_start-1].transaction_id == journal->records[first_appearance_start].transaction_id){
		first_appearance_start--;
	}
	return first_appearance_start;
}


/*
	Returns the offset of the first record.

	In Columnt_t* constraint is the information for our constraint
	call it with NULL if you want all the records in the range.
*/
uint64_t getJournalRecords(Journal_t* journal, uint64_t range_start, uint64_t range_end) {
	uint64_t first_appearance = 0;

	if(journal->tid_index != NULL) {
		bool found = false;
		uint64_t transaction_id = range_start;
		while(found == false && transaction_id <= range_end){
			first_appearance = tidGetHashOffset(journal->tid_index, transaction_id, &found);
			if(found == false){
				transaction_id++;
			} else{
				break;
			}
		}
		if(found == false){
			return journal->num_of_recs;
		}
	} else {
		/*Binary Search for first appearance*/
		uint64_t first = 0;
		uint64_t last = journal->num_of_recs - 1;
		uint64_t middle = (first+last)/2;
		bool not_found = false;

		while (first <= last && not_found == false) {
			if (journal->records[middle].transaction_id < range_start){
				first = middle + 1;    
			}
			else if (journal->records[middle].transaction_id == range_start) {
				first_appearance = middle;
				break;
			}
			else{
				if(middle == 0){
					not_found = true;
					break;
				}
				last = middle - 1;
			}
			middle = (first + last)/2;
		}
		if (first > last || not_found == true){	//Not found
			first_appearance = (last <= first) ? last : first;
			while(first_appearance < journal->num_of_recs && journal->records[first_appearance].transaction_id < range_start){
				first_appearance++;
			}
		}
		if(first_appearance >= journal->num_of_recs){
			return journal->num_of_recs;
		}
		while(first_appearance > 0 && journal->records[first_appearance-1].transaction_id == journal->records[first_appearance].transaction_id){
			first_appearance--;
		}
	}
	return first_appearance;
}

bool checkConstraint(JournalRecord_t* record, Column_t* constraint){
	uint32_t column = constraint->column;
	Op_t operator = constraint->op;
	uint64_t value = constraint->value;
	switch(operator){
		case Equal:
			return (record->column_values[column] == value);
		case NotEqual:
			return (record->column_values[column] != value);
		case Less:
			return (record->column_values[column] < value);
		case LessOrEqual:
			return (record->column_values[column] <= value);
		case Greater:
			return (record->column_values[column] > value);
		case GreaterOrEqual:
			return (record->column_values[column] >= value);
	}
	return false;
}

/*Returns a pointer to the inserted record*/
void insertJournalRecordCopy(Journal_t* journal, JournalRecord_t* old, uint64_t new_transaction_id, bool dirty_bit){
	uint64_t transaction_id = new_transaction_id;
	size_t columns = old->columns;
	uint64_t* column_values = old->column_values;
	insertJournalRecord(journal, transaction_id, columns, column_values, dirty_bit);
}

JournalRecord_t* copyJournalRecord(JournalRecord_t* old){
	JournalRecord_t* new_d = malloc(sizeof(JournalRecord_t));
	ALLOCATION_ERROR(new_d);
	new_d->transaction_id = old->transaction_id;
	new_d->columns = old->columns;
	uint64_t i;
	new_d->column_values = malloc(new_d->columns*sizeof(uint64_t));
	ALLOCATION_ERROR(new_d->column_values);
	for(i = 0; i < new_d->columns; i++){
		new_d->column_values[i] = old->column_values[i];
	}
	new_d->dirty_bit = old->dirty_bit;
	return new_d;
}

int destroyJournalRecord(JournalRecord_t* record){
	free(record->column_values);
	return 0;
}

int destroyJournal(Journal_t* journal) {
	int i;
	for(i=0; i<journal->num_of_recs; i++){
		destroyJournalRecord(&journal->records[i]);
	}
	destroyHash(journal->index);
	if(journal->tid_index != NULL)
		tidDestroyHash(journal->tid_index);
	if(journal->predicate_index != NULL)
		predicateDestroyHash(journal->predicate_index);
	free(journal->records);
	free(journal);
	return 0;
}

void printJournalRecord(JournalRecord_t* rec) {
	size_t i;
	printf("Tr id: %zu\t",rec->transaction_id );
	for(i = 0; i < rec->columns; i++){
		printf("  %zu",rec->column_values[i]);
	}
	printf("\n\n");
}

void printJournal(Journal_t* journal){
	uint64_t i;
	for(i = 0; i < journal->num_of_recs; i++){
		printf("%zu: ",i );
		printJournalRecord(&journal->records[i]);
	}
}