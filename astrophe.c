#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

typedef enum {
  TYPE_NULL,
  TYPE_LIST,
  TYPE_MAP,
  TYPE_OBJECT,
} ObjectType;

typedef struct {
  void *data;
  size_t refcount;
  size_t elemSize;
  size_t count;
  size_t capacity;
  bool recurse;
  ObjectType type;
} Object;

Object *object_create(void *data, ObjectType type) {
  Object *retval = malloc(sizeof(Object));
  retval->data = data;
  retval->refcount = 1;
  retval->elemSize = 0;
  retval->count = 0;
  retval->capacity = 0;
  retval->type = type;
  retval->recurse = false;
  return retval;
}

void object_incref(Object *object) { object->refcount++; }

#define object_ptr(object, type) (type *)object->data
#define object_pat(object, type, idx)                                          \
  (type *)(object->data + idx * sizeof(type))
#define object_val(object, type) *(type *)object->data
#define object_vat(object, type, idx)                                          \
  *(type *)(object->data + idx * sizeof(type))

void object_decref(Object *object) {
  object->refcount--;
  if (object->refcount == 0) {
    if (object->recurse) {
      for (size_t i = 0; i < object->count; i++) {
        Object *current = object_vat(object, Object *, i);
        object_decref(current);
      }
    }
    free(object->data);
    free(object);
  }
}

Object *object_list_create(void const *startingData, size_t len,
                           size_t elemSize) {
  Object *retval = object_create(NULL, TYPE_LIST);
  retval->capacity = len * 2;
  char *data = calloc(retval->capacity, elemSize);
  retval->data = data;
  memcpy(data, startingData, len * elemSize);
  retval->elemSize = elemSize;
  retval->count = len;
  return retval;
}

void object_list_resize(Object *list, size_t size) {
  size_t newsize = size * 2;
  list->data = reallocarray(list->data, newsize, list->elemSize);
  list->capacity = newsize;
}

void object_list_append(Object *list, Object *olist) {
  if (list->capacity <= list->count + olist->count) {
    object_list_resize(list, list->capacity + olist->count);
  }

  memcpy(list->data + list->count * list->elemSize, olist->data,
         olist->count * list->elemSize);
  list->count += olist->count;
}

void object_list_prepend(Object *list, Object *olist) {
  void *newbuf = calloc(list->capacity + olist->count, list->elemSize);
  memcpy(newbuf, olist->data, olist->count * olist->elemSize);
  memcpy(newbuf + olist->count * olist->elemSize, list->data,
         list->count * list->elemSize);
  free(list->data);
  list->data = newbuf;
  list->capacity += olist->count;
  list->count += olist->count;
}

void object_list_trim(Object *object) {
  object->data = reallocarray(object->data, object->count, object->elemSize);
}

void object_list_to_single(Object *list) {
  if (list->type == TYPE_LIST && list->count == 1) {
    object_list_trim(list);
    list->type = TYPE_OBJECT;
    list->count = 1;
    list->capacity = 1;
  }
}

Object *object_list_pop_front(Object *list) {
  Object *retval = object_list_create(list->data, 1, list->elemSize);
  void *newbuf = calloc(list->capacity - 1, list->elemSize);
  list->count--;
  memcpy(newbuf, list->data + list->elemSize, list->count * list->elemSize);
  free(list->data);
  list->data = newbuf;
  object_list_to_single(retval);
  return retval;
}

Object *object_list_pop_back(Object *list) {
  Object *retval =
      object_list_create(list->data + list->count - 1, 1, list->elemSize);
  list->count--;
  object_list_to_single(retval);
  return retval;
}

void object_list_push_front_object(Object *list, Object *value) {
  if (list->recurse) {
    object_incref(value);
    void *newbuf = calloc(list->capacity + 1, list->elemSize);
    list->count++;
    list->capacity++;
    memcpy(newbuf, &value, list->elemSize);
    memcpy(newbuf + list->elemSize, list->data, list->count * list->elemSize);
    free(list->data);
    list->data = newbuf;
  }
}

void object_list_push_back_object(Object *list, Object *value) {
  if (list->recurse) {
    object_incref(value);
    if (list->count >= list->capacity) {
      object_list_resize(list, list->capacity + 1);
    }

    memcpy(list->data + (list->count * list->elemSize), &value, list->elemSize);
    list->count++;
  }
}

Object *object_create_single(void *data, size_t size) {
  Object *retval = object_create(data, TYPE_OBJECT);
  retval->count = 1;
  retval->capacity = 1;
  retval->elemSize = size;
  return retval;
}

Object *object_list_split(Object *list, void *value, size_t vcount) {
  Object *retval = object_list_create(NULL, 0, sizeof(Object *));
  retval->recurse = true;

  void *cursor = list->data;

  for (size_t i = 0; i < list->count; i++) {
    void *current = list->data + i * list->elemSize;
    if (memcmp(current, value, list->elemSize * vcount) == 0) {
      Object *blob = object_list_create(
          cursor, (current - cursor) / list->elemSize, list->elemSize);
      object_list_push_back_object(retval, blob);
      object_decref(blob);
      current += vcount * list->elemSize;
      i += vcount * list->elemSize;
      cursor = current;
    }
  }

  void *endptr = list->data + (list->count * list->elemSize);
  if (cursor < endptr) {
    Object *blob = object_list_create(
        cursor, (endptr - cursor) / list->elemSize, list->elemSize);
    object_list_push_back_object(retval, blob);
    object_decref(blob);
  }

  return retval;
}

#define object_create_string(string)                                           \
  object_list_create(string, strlen(string), 1)
#define object_create_object(obj) object_create_single(obj, sizeof(*obj))

#define object_list_for(list, type)                                            \
  for (                                                                        \
      struct {                                                                 \
        size_t i;                                                              \
        type current;                                                          \
      } loop = {0, object_vat(list, type, 0)};                                 \
      loop.i < list->count; loop.i++,                                          \
        loop.current = object_vat(list, type, MIN(list->count - 1, loop.i)))

#define object_split(list, obj) object_list_split(list, obj->data, obj->count)

int main(int argc, char **argv) {
  Object *str1 = object_create_string("yeeat");
  Object *space = object_create_string(" ");
  Object *ee = object_create_string("ee");
  Object *str2 = object_create_string("spl");

  object_list_append(str1, space);
  object_list_append(str1, str2);
  object_list_prepend(str1, space);
  object_list_prepend(str1, str2);

  Object *split = object_split(str1, space);
  Object *split2 = object_split(str1, ee);
  Object *pf = object_list_pop_front(str1);
  Object *pb = object_list_pop_back(str1);

  printf("Did we garbanzo? %.*s\n", (int)str1->count, object_ptr(str1, char));

  object_list_for(split, Object *) {
    printf("Garble my warble!! %.*s\n", (int)loop.current->count,
           object_ptr(loop.current, char));
  }

  object_list_for(split2, Object *) {
    printf("two!! %.*s\n", (int)loop.current->count,
           object_ptr(loop.current, char));
  }

  printf("Another test: %c %c\n", object_val(pf, char), object_val(pb, char));

  object_decref(str1);
  object_decref(space);
  object_decref(str2);
  object_decref(pf);
  object_decref(pb);
  object_decref(split);
  object_decref(split2);
  object_decref(ee);

  return 0;
}
