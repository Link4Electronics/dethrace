#include "brlists.h"

#include <unistd.h>

char rscid[49];

// Offset: 10
// Size: 60
void BrNewList(br_list* list) {
    list->_null = NULL;
    list->tail = (br_node*)list;
    list->head = list->_null;
}

// Offset: 80
// Size: 68
void BrAddHead(br_list* list, br_node* node) {
}

// Offset: 158
// Size: 73
void BrAddTail(br_list* list, br_node* node) {
}

// Offset: 241
// Size: 77
br_node* BrRemHead(br_list* list) {
    br_node* n;
}

// Offset: 328
// Size: 77
br_node* BrRemTail(br_list* list) {
    br_node* n;
}

// Offset: 414
// Size: 68
void BrInsert(br_list* list, br_node* here, br_node* node) {
}

// Offset: 491
// Size: 66
br_node* BrRemove(br_node* node) {
}

// Offset: 573
// Size: 39
void BrSimpleNewList(br_simple_list* list) {
    list->head = NULL;
}

// Offset: 628
// Size: 76
void BrSimpleAddHead(br_simple_list* list, br_simple_node* node) {
    node->next = list->head;
    node->prev = &list->head;
    if (node->next) {
        node->next->prev = &node;
    }
    list->head = node;
}

// Offset: 720
// Size: 65
br_simple_node* BrSimpleRemHead(br_simple_list* list) {
    br_simple_node* node;
}

// Offset: 800
// Size: 76
void BrSimpleInsert(br_simple_list* list, br_simple_node* here, br_simple_node* node) {
}

// Offset: 891
// Size: 93
br_simple_node* BrSimpleRemove(br_simple_node* node) {
}
