#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <bt-memory.h>

#define SB_BASE_SIZE 0x00001000
typedef struct bt_string_builder {
    char *string;
    size_t length;
    size_t size;
} bt_string_builder;

bt_string_builder *
bt_string_builder_new(void)
{
    bt_string_builder *builder;
    // Allocate space for the builder itself
    builder = bt_malloc(sizeof(*builder));
    if (builder == NULL)
        return NULL;
    // Initialize the length to `0' of course
    builder->length = 0;
    // Allocate initial space to avoid too many calls to `bt_malloc()`
    builder->string = bt_malloc(SB_BASE_SIZE);
    if (builder->string == NULL) {
        // If it failed, no big deal. We can try later
        builder->size = 0;
    } else {
        // `null` terminate it so it's always a valid string
        builder->string[0] = '\0';
        // Initialize the size parameter in order to know how
        // much space is left
        builder->size = SB_BASE_SIZE;
    }
    return builder;
}

void
bt_string_builder_reset(bt_string_builder *builder)
{
    builder->length = 0;
}

void
bt_string_builder_free(bt_string_builder *builder)
{
    // Simple and self-explanatory `free()` function
    if (builder == NULL)
        return;
    bt_free(builder->string);
    bt_free(builder);
}

char *
bt_string_builder_take_string(bt_string_builder *builder)
{
    char *string;
    if (builder == NULL)
        return NULL;
    // Extract the string from the builder
    string = builder->string;
    // TODO: Make `reset` the builder a real reset

    // Reset the builder
    builder->string = NULL;
    builder->size = 0;
    builder->length = 0;

    return string;
}

void
bt_string_builder_append(bt_string_builder *builder, const char *const chunk, size_t length)
{
    // Check if the new data fits
    if (length + builder->length + 1 >= builder->size) {
        size_t size;
        void *data;

        // It doesn't fit, compute how much more space is needed
        size = length + builder->length + 1;
        // Calculate how many `SB_BASE_SIZE` is good
        size = SB_BASE_SIZE * (size / SB_BASE_SIZE + 1);
        // Allocate the new space
        data = bt_realloc(builder->string, size);
        if (data == NULL)
            return;
        // Reset the values
        builder->string = data;
        builder->size = size;
    }
    // Copy the chunk to the builder `string` member
    memcpy(builder->string + builder->length, chunk, length);
    // Update the length
    builder->length += length;
    // And always `null` terminate
    builder->string[builder->length] = '\0';
}

void
bt_string_builder_append_string(bt_string_builder *builder, const char *const chunk)
{
    size_t length;
    // WTF?
    if (chunk == NULL)
        return;
    // Calculate the length
    length = strlen(chunk);
    if (length == 0)
        return;
    // Use the function that does this correctly
    bt_string_builder_append(builder, chunk, length);
}

void
bt_string_builder_vprintf(bt_string_builder *builder, const char *const format, va_list list)
{
    ssize_t size;
    va_list copy;

    // Make a copy because `vsnprintf()` will screw up `list`
    va_copy(copy, list);
    size = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    // If no error, and we do have something to copy
    if (size > 0) { // FIXME: taking advantage of the stack hoping
                    //        size doesn't exceed the stack size.
        char buffer[size + 1];
        // Print the string with the format
        vsnprintf(buffer, size + 1, format, list);
        // Copy the printed chunk to the builder.
        bt_string_builder_append(builder, buffer, size);
    }
}

void
bt_string_builder_printf(bt_string_builder *builder, const char *const format, ...)
{
    va_list args;
    // Initialize variable arguments
    va_start(args, format);
    // Call the function that takes a list
    bt_string_builder_vprintf(builder, format, args);
    // Clean up
    va_end(args);
}

const char *
bt_string_builder_string(const bt_string_builder *const builder)
{
    if (builder == NULL)
        return NULL;
    return builder->string;
}
