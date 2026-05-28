#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/slab.h>
#define CMA_MEM_SIZE (500 * 1024 * 1024) // 512MB
static void *cma_cpu_addr = NULL;
static dma_addr_t cma_dma_handle;
static struct platform_device *cma_pdev;
// DMA 掩码声明为静态全局变量，防止 init 返回后栈指针悬空
static u64 cma_dma_mask = DMA_BIT_MASK(64);
// 处理用户态的 mmap 请求
static int cma_mmap(struct file *filp, struct vm_area_struct *vma)
{
    unsigned long size = vma->vm_end - vma->vm_start;
    // 严格的边界与偏移校验，防止用户态恶意越界触发内核 Oops
    if (vma->vm_pgoff != 0) {
        printk(KERN_WARNING "Avionics CMA: 拒绝非零偏移的 mmap 请求\n");
        return -EINVAL;
    }
    if (size > CMA_MEM_SIZE) {
        printk(KERN_WARNING "Avionics CMA: 映射请求超限 (%lu > %lu)\n",
               size, (unsigned long)CMA_MEM_SIZE);
        return -EINVAL;
    }
    // 使用 DMA API 标准映射函数，自动处理缓存一致性与物理连续性
    return dma_mmap_coherent(&cma_pdev->dev, vma, cma_cpu_addr,
                             cma_dma_handle, size);
}
static const struct file_operations cma_fops = {
    .owner = THIS_MODULE,
    .mmap  = cma_mmap,
};
static struct miscdevice cma_misc = {
    .minor = MISC_DYNAMIC_MINOR,
    .name  = "avionics_cma",   // 设备节点: /dev/avionics_cma
    .fops  = &cma_fops,
};
static int __init avionics_cma_init(void)
{
    int ret;
    // 1. 分配并注册虚拟平台设备（规范的生命周期管理）
    cma_pdev = platform_device_alloc("avionics_cma_dev", PLATFORM_DEVID_NONE);
    if (!cma_pdev) {
        printk(KERN_ERR "Avionics CMA: 分配 platform_device 失败\n");
        return -ENOMEM;
    }
    // 2. 绑定静态 DMA 掩码并添加到内核设备模型
    cma_pdev->dev.dma_mask = &cma_dma_mask;
    cma_pdev->dev.coherent_dma_mask = cma_dma_mask;
    ret = platform_device_add(cma_pdev);
    if (ret) {
        printk(KERN_ERR "Avionics CMA: 注册 platform_device 失败\n");
        platform_device_put(cma_pdev);
        return ret;
    }
    // 3. 从 CMA 区域分配连续物理内存
    cma_cpu_addr = dma_alloc_coherent(&cma_pdev->dev, CMA_MEM_SIZE,
                                      &cma_dma_handle, GFP_KERNEL);
    if (!cma_cpu_addr) {
        printk(KERN_ERR "Avionics CMA: 无法分配 %lu MB 的连续内存 "
               "(请检查 GRUB 中是否已添加 cma=512M)\n",
               (unsigned long)CMA_MEM_SIZE / (1024 * 1024));
        platform_device_unregister(cma_pdev);
        return -ENOMEM;
    }
    // 4. 注册字符设备供用户态访问
    ret = misc_register(&cma_misc);
    if (ret) {
        printk(KERN_ERR "Avionics CMA: 字符设备注册失败\n");
        dma_free_coherent(&cma_pdev->dev, CMA_MEM_SIZE,
                          cma_cpu_addr, cma_dma_handle);
        platform_device_unregister(cma_pdev);
        return ret;
    }
    printk(KERN_INFO "Avionics CMA: 成功分配 512MB 连续物理内存, "
           "物理地址: %pad\n", &cma_dma_handle);
    return 0;
}
static void __exit avionics_cma_exit(void)
{
    misc_deregister(&cma_misc);
    if (cma_cpu_addr) {
        dma_free_coherent(&cma_pdev->dev, CMA_MEM_SIZE,
                          cma_cpu_addr, cma_dma_handle);
    }
    platform_device_unregister(cma_pdev);
    printk(KERN_INFO "Avionics CMA: 模块已卸载，内存与设备已彻底清理\n");
}
module_init(avionics_cma_init);
module_exit(avionics_cma_exit);
MODULE_LICENSE("GPL");
