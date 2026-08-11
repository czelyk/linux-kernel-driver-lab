#include <stdio.h>
#include <stddef.h>

struct bad_layout {
    char a;
    int b;
    char c;
};

struct good_layout {
    int b;
    char a;
    char c;
};

int main(void)
{
    printf("char: %zu byte\n", sizeof(char));
    printf("int : %zu byte\n\n", sizeof(int));

    printf("bad_layout size = %zu\n", sizeof(struct bad_layout));
    printf("  a offset = %zu\n", offsetof(struct bad_layout, a));
    printf("  b offset = %zu\n", offsetof(struct bad_layout, b));
    printf("  c offset = %zu\n\n", offsetof(struct bad_layout, c));

    printf("good_layout size = %zu\n", sizeof(struct good_layout));
    printf("  b offset = %zu\n", offsetof(struct good_layout, b));
    printf("  a offset = %zu\n", offsetof(struct good_layout, a));
    printf("  c offset = %zu\n", offsetof(struct good_layout, c));

    return 0;
}