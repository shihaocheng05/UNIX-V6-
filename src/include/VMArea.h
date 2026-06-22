#ifndef VM_AREA_H
#define VM_AREA_H

/*由于vm采用更加细化的缺页异常处理方式，单纯使用MemoryDescriptor已经不足以管理虚空间*/
struct vm_area {
      unsigned long  v_start;                    //虚拟地址起址
      unsigned long  v_length;                   //虚空间长度
      unsigned int   f_offset;                   //文件内偏移 (字节)
      unsigned int   f_length;                   //文件内实际长度 (字节)
      unsigned char  v_Present      : 1;
      unsigned char  v_ReadWriter    : 1;
      unsigned char  v_UserSupervisor : 1;
      unsigned char  v_reserved      : 5;        //预留扩展位
      struct vm_area* next;                   
};    //24B

#endif

