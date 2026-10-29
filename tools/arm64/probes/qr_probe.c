#include <stdio.h>
#include "qrencode.h"
int main(void) {
    QRcode *code = QRcode_encodeString8bit("Ditto ARM64 dependency audit", 0, QR_ECLEVEL_M);
    if (!code || code->width <= 0 || !code->data) return 1;
    printf("PASS: bundled QR encoder produced a %d by %d matrix\n", code->width, code->width);
    QRcode_free(code);
    return 0;
}
