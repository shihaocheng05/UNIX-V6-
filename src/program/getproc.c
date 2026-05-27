#include <stdio.h>
#include "sys.h"

/* 简单的十六进制转换（不依赖 printf 的格式化功能） */
static void ultohex(unsigned long v, char* buf)
{
    const char* hex = "0123456789abcdef";
    char tmp[32];
    int i = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (v && i < 32) { tmp[i++] = hex[v & 0xF]; v >>= 4; }
    int p = 0;
    while (i) buf[p++] = tmp[--i];
    buf[p] = 0;
}

static void uitoa_dec(unsigned int v, char* buf)
{
    char tmp[16];
    int i = 0;
    if (v == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (v && i < 16) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    int p = 0;
    while (i) buf[p++] = tmp[--i];
    buf[p] = 0;
}

static void puts_raw(const char* s)
{
    /* 使用 write 直接输出，避免 printf 解析问题 */
    int len = 0; while (s[len]) len++;
    write(1, (char*)s, len); /* 直接使用文件描述符1，避免宏未解析问题 */
}

static void put_line(const char* label, const char* value)
{
    puts_raw(label);
    puts_raw(value);
    /* 使用 \r\n 兼容可能只识别 CR 的显示设备 */
    puts_raw("\r\n");
}

int main1()
{
    unsigned int x_size = 0, p_size = 0;
    unsigned long x_caddr = 0, p_addr = 0;

    if (getproc(&x_caddr, &x_size, &p_addr, &p_size) != 0)
    {
        puts_raw("getproc failed\r\n");
        return -1;
    }

    unsigned long code_start = (unsigned long)&main1;
    unsigned long code_align = (code_start + 4095UL) & ~4095UL;
    unsigned long v_data_addr = code_align + x_size;

    char buf_cs[32], buf_xsz[16], buf_vd[32], buf_psz[16], buf_xc[32], buf_pa[32];
    ultohex(code_start, buf_cs);
    uitoa_dec(x_size, buf_xsz);
    ultohex(v_data_addr, buf_vd);
    uitoa_dec(p_size, buf_psz);
    ultohex(x_caddr, buf_xc);
    ultohex(p_addr, buf_pa);

    put_line("code_start(hex)=", buf_cs);
    put_line("code_size(dec)=", buf_xsz);
    put_line("data_start(hex)=", buf_vd);
    put_line("p_size(dec)=", buf_psz);
    put_line("x_caddr(hex)=", buf_xc);
    put_line("p_addr(hex)=", buf_pa);

    return 0;
}
