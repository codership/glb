/*-
 * Copyright (c) 1992, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _LIST_H_
#define _LIST_H_

/* Linked-list implementation from BSD */


/*
 * Singly-linked List definitions.
 */
#define SLLIST_HEAD(name, type)                                          \
struct name {                                                           \
        struct type *slh_first; /* first element */                     \
}

#define SLLIST_ENTRY(type)                                               \
struct {                                                                \
        struct type *sle_next;  /* next element */                      \
}

/*
 * Singly-linked List functions.
 */
#define SLLIST_EMPTY(head)       ((head)->slh_first == NULL)

#define SLLIST_FIRST(head)       ((head)->slh_first)

#define SLLIST_FOREACH(var, head, field)                                 \
        for((var) = (head)->slh_first; (var); (var) = (var)->field.sle_next)

#define SLLIST_INIT(head) {                                              \
        (head)->slh_first = NULL;                                       \
}

#define SLLIST_INSERT_AFTER(slistelm, elm, field) do {                   \
        (elm)->field.sle_next = (slistelm)->field.sle_next;             \
        (slistelm)->field.sle_next = (elm);                             \
} while (0)

#define SLLIST_INSERT_HEAD(head, elm, field) do {                        \
        (elm)->field.sle_next = (head)->slh_first;                      \
        (head)->slh_first = (elm);                                      \
} while (0)

#define SLLIST_NEXT(elm, field)  ((elm)->field.sle_next)

#define SLLIST_REMOVE_HEAD(head, field) do {                             \
        (head)->slh_first = (head)->slh_first->field.sle_next;          \
} while (0)

#define SLLIST_REMOVE(head, elm, type, field) do {                       \
        if ((head)->slh_first == (elm)) {                               \
                SLLIST_REMOVE_HEAD((head), field);                       \
        }                                                               \
        else {                                                          \
                struct type *curelm = (head)->slh_first;                \
                while( curelm->field.sle_next != (elm) )                \
                        curelm = curelm->field.sle_next;                \
                curelm->field.sle_next =                                \
                    curelm->field.sle_next->field.sle_next;             \
        }                                                               \
} while (0)

/*
 * Singly-linked Tail queue definitions.
 */
#define STAILQ_HEAD(name, type)                                         \
struct name {                                                           \
        struct type *stqh_first;/* first element */                     \
        struct type **stqh_last;/* addr of last next element */         \
}

#define STAILQ_HEAD_INITIALIZER(head)                                   \
        { NULL, &(head).stqh_first }

#define STAILQ_ENTRY(type)                                              \
struct {                                                                \
        struct type *stqe_next; /* next element */                      \
}

/*
 * Singly-linked Tail queue functions.
 */
#define STAILQ_EMPTY(head) ((head)->stqh_first == NULL)

#define STAILQ_INIT(head) do {                                          \
        (head)->stqh_first = NULL;                                      \
        (head)->stqh_last = &(head)->stqh_first;                        \
} while (0)

#define STAILQ_FIRST(head)      ((head)->stqh_first)
#define STAILQ_LAST(head)       (*(head)->stqh_last)

#define STAILQ_INSERT_HEAD(head, elm, field) do {                       \
        if (((elm)->field.stqe_next = (head)->stqh_first) == NULL)      \
                (head)->stqh_last = &(elm)->field.stqe_next;            \
        (head)->stqh_first = (elm);                                     \
} while (0)

#define STAILQ_INSERT_TAIL(head, elm, field) do {                       \
        (elm)->field.stqe_next = NULL;                                  \
        *(head)->stqh_last = (elm);                                     \
        (head)->stqh_last = &(elm)->field.stqe_next;                    \
} while (0)

#define STAILQ_INSERT_AFTER(head, tqelm, elm, field) do {               \
        if (((elm)->field.stqe_next = (tqelm)->field.stqe_next) == NULL)\
                (head)->stqh_last = &(elm)->field.stqe_next;            \
        (tqelm)->field.stqe_next = (elm);                               \
} while (0)

#define STAILQ_NEXT(elm, field) ((elm)->field.stqe_next)

#define STAILQ_REMOVE_HEAD(head, field) do {                            \
        if (((head)->stqh_first =                                       \
             (head)->stqh_first->field.stqe_next) == NULL)              \
                (head)->stqh_last = &(head)->stqh_first;                \
} while (0)

#define STAILQ_REMOVE_HEAD_UNTIL(head, elm, field) do {                 \
        if (((head)->stqh_first = (elm)->field.stqe_next) == NULL)      \
                (head)->stqh_last = &(head)->stqh_first;                \
} while (0)


#define STAILQ_REMOVE(head, elm, type, field) do {                      \
        if ((head)->stqh_first == (elm)) {                              \
                STAILQ_REMOVE_HEAD(head, field);                        \
        }                                                               \
        else {                                                          \
                struct type *curelm = (head)->stqh_first;               \
                while( curelm->field.stqe_next != (elm) )               \
                        curelm = curelm->field.stqe_next;               \
                if((curelm->field.stqe_next =                           \
                    curelm->field.stqe_next->field.stqe_next) == NULL)  \
                        (head)->stqh_last = &(curelm)->field.stqe_next; \
        }                                                               \
} while (0)

/*
 * List definitions.
 */
#undef LIST_HEAD
#define LIST_HEAD(name, type)                                           \
struct name {                                                           \
    struct type *lh_first;  /* first element */                     \
}

#undef LIST_HEAD_INITIALIZER
#define LIST_HEAD_INITIALIZER(head)                                     \
        { NULL }

#undef LIST_ENTRY
#define LIST_ENTRY(type)                                                \
struct {                                                                \
        struct type *le_next;   /* next element */                      \
        struct type **le_prev;  /* address of previous next element */  \
}

/*
 * List functions.
 */

#undef LIST_INIT
#define LIST_INIT(head) do {                                            \
        (head)->lh_first = NULL;                                        \
} while (/*CONSTCOND*/0)

#undef LIST_INSERT_AFTER
#define LIST_INSERT_AFTER(listelm, elm, field) do {                     \
        if (((elm)->field.le_next = (listelm)->field.le_next) != NULL)  \
                (listelm)->field.le_next->field.le_prev =               \
                    &(elm)->field.le_next;                              \
        (listelm)->field.le_next = (elm);                               \
        (elm)->field.le_prev = &(listelm)->field.le_next;               \
} while (/*CONSTCOND*/0)

#undef LIST_INSERT_BEFORE
#define LIST_INSERT_BEFORE(listelm, elm, field) do {                    \
        (elm)->field.le_prev = (listelm)->field.le_prev;                \
        (elm)->field.le_next = (listelm);                               \
        *(listelm)->field.le_prev = (elm);                              \
        (listelm)->field.le_prev = &(elm)->field.le_next;               \
} while (/*CONSTCOND*/0)

#undef LIST_INSERT_HEAD
#define LIST_INSERT_HEAD(head, elm, field) do {                         \
        if (((elm)->field.le_next = (head)->lh_first) != NULL)          \
                (head)->lh_first->field.le_prev = &(elm)->field.le_next;\
        (head)->lh_first = (elm);                                       \
        (elm)->field.le_prev = &(head)->lh_first;                       \
} while (/*CONSTCOND*/0)

#undef LIST_REMOVE
#define LIST_REMOVE(elm, field) do {                                    \
        if ((elm)->field.le_next != NULL)                               \
                (elm)->field.le_next->field.le_prev =                   \
                    (elm)->field.le_prev;                               \
        *(elm)->field.le_prev = (elm)->field.le_next;                   \
        (elm)->field.le_prev = NULL;                                    \
        (elm)->field.le_next = NULL;                                    \
} while (/*CONSTCOND*/0)

#undef LIST_FOREACH
#define LIST_FOREACH(var, head, field)                                  \
        for ((var) = ((head)->lh_first);                                \
                (var);                                                  \
                (var) = ((var)->field.le_next))

#undef LIST_FOREACH_SAFE
#define LIST_FOREACH_SAFE(var, head, field, tvar)                       \
        for ((var) = LIST_FIRST((head));                                \
            (var) && ((tvar) = LIST_NEXT((var), field), 1);             \
            (var) = (tvar))

/*
 * List access methods.
 */
#undef LIST_EMPTY
#define LIST_EMPTY(head)                ((head)->lh_first == NULL)
#undef LIST_FIRST
#define LIST_FIRST(head)                ((head)->lh_first)
#undef LIST_NEXT
#define LIST_NEXT(elm, field)           ((elm)->field.le_next)

/*
 * Tail queue definitions.
 */
#define TAILQ_HEAD(name, type)                                          \
struct name {                                                           \
        struct type *tqh_first; /* first element */                     \
        struct type **tqh_last; /* addr of last next element */         \
}

#define TAILQ_ENTRY(type)                                               \
struct {                                                                \
        struct type *tqe_next;  /* next element */                      \
        struct type **tqe_prev; /* address of previous next element */  \
}

/*
 * Tail queue functions.
 */
#define TAILQ_INIT(head) {                                              \
        (head)->tqh_first = NULL;                                       \
        (head)->tqh_last = &(head)->tqh_first;                          \
}

#define TAILQ_INSERT_HEAD(head, elm, field) {                           \
        if (((elm)->field.tqe_next = (head)->tqh_first) != NULL)        \
                (elm)->field.tqe_next->field.tqe_prev =                 \
                    &(elm)->field.tqe_next;                             \
        else                                                            \
                (head)->tqh_last = &(elm)->field.tqe_next;              \
        (head)->tqh_first = (elm);                                      \
        (elm)->field.tqe_prev = &(head)->tqh_first;                     \
}

#define TAILQ_INSERT_TAIL(head, elm, field) {                           \
        (elm)->field.tqe_next = NULL;                                   \
        (elm)->field.tqe_prev = (head)->tqh_last;                       \
        *(head)->tqh_last = (elm);                                      \
        (head)->tqh_last = &(elm)->field.tqe_next;                      \
}

#define TAILQ_INSERT_AFTER(head, listelm, elm, field) {                 \
        if (((elm)->field.tqe_next = (listelm)->field.tqe_next) != NULL)\
                (elm)->field.tqe_next->field.tqe_prev =                 \
                    &(elm)->field.tqe_next;                             \
        else                                                            \
                (head)->tqh_last = &(elm)->field.tqe_next;              \
        (listelm)->field.tqe_next = (elm);                              \
        (elm)->field.tqe_prev = &(listelm)->field.tqe_next;             \
}

#define TAILQ_REMOVE(head, elm, field) {                                \
        if (((elm)->field.tqe_next) != NULL)                            \
                (elm)->field.tqe_next->field.tqe_prev =                 \
                    (elm)->field.tqe_prev;                              \
        else                                                            \
                (head)->tqh_last = (elm)->field.tqe_prev;               \
        *(elm)->field.tqe_prev = (elm)->field.tqe_next;                 \
}

#define TAILQ_FOREACH(var, head, field)                                 \
        for ((var) = ((head)->tqh_first);                               \
            (var);                                                      \
        (var) = ((var)->field.tqe_next))

#define TAILQ_FOREACH_SAFE(var, head, field, tvar)                      \
        for ((var) = TAILQ_FIRST((head));                               \
                (var) && ((tvar) = TAILQ_NEXT((var), field), 1);        \
                (var) = (tvar))

#define TAILQ_FOREACH_REVERSE(var, head, headname, field)               \
        for ((var) = (*(((struct headname *)((head)->tqh_last))->tqh_last));    \
                (var);                                                  \
                (var) = (*(((struct headname *)((var)->field.tqe_prev))->tqh_last)))

#define TAILQ_FOREACH_REVERSE_SAFE(var, head, headname, field, tvar)    \
        for ((var) = TAILQ_LAST((head), headname);                      \
                (var) && ((tvar) = TAILQ_PREV((var), headname, field), 1);  \
                (var) = (tvar))

/*
 * Tail queue access methods.
 */

#define TAILQ_EMPTY(head)               ((head)->tqh_first == NULL)
#define TAILQ_FIRST(head)               ((head)->tqh_first)
#define TAILQ_NEXT(elm, field)          ((elm)->field.tqe_next)
#define TAILQ_LAST(head, headname) \
        (*(((struct headname *)((head)->tqh_last))->tqh_last))
#define TAILQ_PREV(elm, headname, field) \
        (*(((struct headname *)((elm)->field.tqe_prev))->tqh_last))

#endif // _LIST_H_
