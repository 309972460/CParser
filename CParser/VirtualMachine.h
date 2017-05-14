#ifndef __VM_H
#define __VM_H

#include "types.h"
#include "memory.h"

using namespace clib::type;
using namespace clib::memory;

// Website: https://github.com/bajdcc/MiniOS

/* virtual memory management */
// �����䣨����ҳ����䷽ʽ��

// �ο���http://wiki.osdev.org/Paging

// ����һ��32λ�����ַ��virtual address��
// 32-22: ҳĿ¼�� | 21-12: ҳ��� | 11-0: ҳ��ƫ��
// http://www.360doc.com/content/11/0804/10/7204565_137844381.shtml

/* 4k per page */
#define PAGE_SIZE 4096 

/* ҳ���룬ȡ��20λ */
#define PAGE_MASK 0xfffff000

/* ��ַ���� */
#define PAGE_ALIGN_DOWN(x) ((x) & PAGE_MASK)
#define PAGE_ALIGN_UP(x) (((x) + PAGE_SIZE - 1) & PAGE_MASK)

/* ������ַ */
#define PDE_INDEX(x) (((x) >> 22) & 0x3ff)  // ��õ�ַ��Ӧ��ҳĿ¼��
#define PTE_INDEX(x) (((x) >> 12) & 0x3ff)  // ���ҳ���
#define OFFSET_INDEX(x) ((x) & 0xfff)       // ���ҳ��ƫ��

// ҳĿ¼�ҳ������uint32��ʾ����
typedef uint32_t pde_t;
typedef uint32_t pte_t;

/* ҳĿ¼��С 1024 */
#define PDE_SIZE (PAGE_SIZE/sizeof(pte_t))
/* ҳ���С 1024 */
#define PTE_SIZE (PAGE_SIZE/sizeof(pde_t))
/* ҳ������ 1024*PTE_SIZE*PAGE_SIZE = 4G */
#define PTE_COUNT 1024

/* CPU */
#define CR0_PG  0x80000000 

/* pde&pdt attribute */
#define PTE_P   0x1     // ��Чλ Present
#define PTE_R   0x2     // ��дλ Read/Write, can be read&write when set
#define PTE_U   0x4     // �û�λ User / Kern
#define PTE_K   0x0     // �ں�λ User / Kern
#define PTE_W   0x8     // д�� Write through
#define PTE_D   0x10    // ������ Cache disable
#define PTE_A   0x20    // �ɷ��� Accessed
#define PTE_S   0x40    // Page size, 0 for 4kb pre page
#define PTE_G   0x80    // Ignored

/* �û�����λ�ַ */
#define USER_BASE 0xc0000000
/* �û����ݶλ�ַ */
#define DATA_BASE 0xd0000000
/* �û�ջ��ַ */
#define STACK_BASE 0xe0000000

struct __page__
{
    byte bits[4096];
};

class CVirtualMachine
{
public:
    CVirtualMachine(std::vector<LEX_T(int)>, std::vector<LEX_T(char)>);
    ~CVirtualMachine();

    void exec(int entry);

private:
    // ����ҳ��
    uint32_t pmm_alloc();
    // ��ʼ��ҳ��
    void vmm_init();
    // ��ҳӳ��
    void vmm_map(uint32_t va, uint32_t pa, uint32_t flags);
    // ���ӳ��
    void vmm_unmap(pde_t *pgdir, uint32_t va);
    // ��ѯ��ҳ���
    int vmm_ismap(uint32_t va, uint32_t *pa) const;

    template<class T = int> T vmm_get(uint32_t va);
    char* vmm_getstr(uint32_t va);
    template<class T = int> T vmm_set(uint32_t va, T);

private:
    /* �ں�ҳ�� = PTE_SIZE*PAGE_SIZE */
    pde_t *pgd_kern;
    /* �ں�ҳ������ = PTE_COUNT*PTE_SIZE*PAGE_SIZE */
    pde_t *pte_kern;
    /* ����16MB�����ڴ�(1 block=16B) */
    memory_pool<1024 * 1024> memory;
    /* ҳ�� */
    pde_t *pgdir{ nullptr };
};

#endif
