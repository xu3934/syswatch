#include "syswatch.h"

#include <ctype.h>
#include <stdio.h>

const char *format_bytes(unsigned long long bytes, char *buf, size_t size)
{
    static const char *units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = (double)bytes;
    size_t unit = 0;

    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        unit++;
    }
    if (unit == 0)
        snprintf(buf, size, "%llu %s", bytes, units[unit]);
    else
        snprintf(buf, size, "%.1f %s", value, units[unit]);
    return buf;
}

int is_numeric_name(const char *name)
{
    if (name == NULL || *name == '\0')
        return 0;
    for (; *name != '\0'; name++) {
        if (!isdigit((unsigned char)*name))
            return 0;
    }
    return 1;
}
