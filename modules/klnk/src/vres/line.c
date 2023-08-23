#include "line.h"

vres_digest_t vres_line_get_digest(char *buf)
{
    int i, pos = 0;
    vres_digest_t digest = 0;
    size_t buflen = VRES_LINE_SIZE;

    if (buflen < sizeof(vres_digest_t) * VRES_LINE_NR_SAMPLES) {
        while (pos < buflen) {
            if (pos + sizeof(vres_digest_t) < buflen)
                digest ^= *(vres_digest_t *)&buf[pos];
            else {
                vres_digest_t val;

                memcpy(&val, &buf[pos], buflen - pos);
                digest ^= val;
            }
            pos += sizeof(vres_digest_t);
        }
    } else {
        int list[VRES_LINE_NR_SAMPLES] = {0};

        for (i = 0; i < VRES_LINE_NR_SAMPLES; i++) {
            int start = pos;

            do {
                vres_digest_t val;

                if (pos + sizeof(vres_digest_t) < buflen)
                    memcpy(&val, &buf[pos], sizeof(vres_digest_t));
                else
                    memcpy(&val, &buf[pos], buflen - pos);

                if (val) {
                    int j;

                    for (j = 0; j < i; j++)
                        if (list[j] == val)
                            break;

                    if (j == i) {
                        digest ^= val;
                        list[i] = val;
                        pos = (pos + val) % buflen;
                        break;
                    }
                }
                pos++;
                if (pos == buflen)
                    pos = 0;
            } while (pos != start);

            if (pos == start)
                break;
        }
    }

    return digest;
}
