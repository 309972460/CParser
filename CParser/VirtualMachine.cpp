#include "stdafx.h"
#include "VirtualMachine.h"

uint32_t CVirtualMachine::pmm_alloc()
{
    auto page = PAGE_ALIGN_UP((uint32_t)memory.alloc_array<byte>(PAGE_SIZE * 2));
    memset((void*)page, 0, PAGE_SIZE);
    return page;
}

void CVirtualMachine::vmm_init() {
    pgd_kern = (pde_t *)malloc(PTE_SIZE * sizeof(pde_t));
    memset(pgd_kern, 0, PTE_SIZE * sizeof(pde_t));
    pte_kern = (pte_t *)malloc(PTE_COUNT*PTE_SIZE * sizeof(pte_t));
    memset(pte_kern, 0, PTE_COUNT*PTE_SIZE * sizeof(pte_t));
    pgdir = pgd_kern;

    uint32_t i;

    // map 4G memory, physcial address = virtual address
    for (i = 0; i < PTE_SIZE; i++) {
        pgd_kern[i] = (uint32_t)pte_kern[i] | PTE_P | PTE_R | PTE_K;
    }

    uint32_t *pte = (uint32_t *)pte_kern;
    for (i = 1; i < PTE_COUNT*PTE_SIZE; i++) {
        pte[i] = (i << 12) | PTE_P | PTE_R | PTE_K; // i��ҳ���
    }
}

// ��ҳӳ��
// va = �����ַ  pa = �����ַ
void CVirtualMachine::vmm_map(uint32_t va, uint32_t pa, uint32_t flags) {
    uint32_t pde_idx = PDE_INDEX(va); // ҳĿ¼��
    uint32_t pte_idx = PTE_INDEX(va); // ҳ���

    pte_t *pte = (pte_t *)(pgdir[pde_idx] & PAGE_MASK); // ҳ��

    if (!pte) { // ȱҳ
        if (va >= USER_BASE) { // �����û���ַ��ת��
            pte = (pte_t *)pmm_alloc(); // ��������ҳ��������ҳ��
            pgdir[pde_idx] = (uint32_t)pte | PTE_P | flags; // ����ҳ��
            pte[pte_idx] = (pa & PAGE_MASK) | PTE_P | flags; // ����ҳ����
        }
        else { // �ں˵�ַ��ת��
            pte = (pte_t *)(pgd_kern[pde_idx] & PAGE_MASK); // ȡ���ں�ҳ��
            pgdir[pde_idx] = (uint32_t)pte | PTE_P | flags; // ����ҳ��
        }
    }
    else { // pte����
        pte[pte_idx] = (pa & PAGE_MASK) | PTE_P | flags; // ����ҳ����
    }
}

// �ͷ���ҳ
void CVirtualMachine::vmm_unmap(pde_t *pde, uint32_t va) {
    uint32_t pde_idx = PDE_INDEX(va);
    uint32_t pte_idx = PTE_INDEX(va);

    pte_t *pte = (pte_t *)(pde[pde_idx] & PAGE_MASK);

    if (!pte) {
        return;
    }

    pte[pte_idx] = 0; // ���ҳ�����ʱ��ЧλΪ��
}

// �Ƿ��ѷ�ҳ
int CVirtualMachine::vmm_ismap(uint32_t va, uint32_t *pa) const {
    uint32_t pde_idx = PDE_INDEX(va);
    uint32_t pte_idx = PTE_INDEX(va);

    pte_t *pte = (pte_t *)(pgdir[pde_idx] & PAGE_MASK);
    if (!pte) {
        return 0; // ҳ������
    }
    if (pte[pte_idx] != 0 && (pte[pte_idx] & PTE_P) && pa) {
        *pa = pte[pte_idx] & PAGE_MASK; // ��������ҳ��
        return 1; // ҳ�����
    }
    return 0; // ҳ�������
}

char* CVirtualMachine::vmm_getstr(uint32_t va)
{
    uint32_t pa;
    if (vmm_ismap(va, &pa))
    {
        return (char*)pa + OFFSET_INDEX(va);
    }
    vmm_map(va, pmm_alloc(), PTE_U | PTE_P | PTE_R);
    assert(0);
    return vmm_getstr(va);
}

template<class T>
T CVirtualMachine::vmm_get(uint32_t va)
{
    uint32_t pa;
    if (vmm_ismap(va, &pa))
    {
        return *((T*)pa + OFFSET_INDEX(va));
    }
    vmm_map(va, pmm_alloc(), PTE_U | PTE_P | PTE_R);
    assert(0);
    return vmm_get<T>(va);
}

template<class T>
T CVirtualMachine::vmm_set(uint32_t va, T value)
{
    uint32_t pa;
    if (vmm_ismap(va, &pa))
    {
        *((T*)pa + OFFSET_INDEX(va)) = value;
        return value;
    }
    vmm_map(va, pmm_alloc(), PTE_U | PTE_P | PTE_R);
    assert(0);
    return vmm_set(va, value);
}

//-----------------------------------------


CVirtualMachine::CVirtualMachine(std::vector<LEX_T(int)> text, std::vector<LEX_T(char)> data)
{
    vmm_init();
    uint32_t pa;
    /* ӳ��4KB�Ĵ���ռ� */
    vmm_map(USER_BASE, (uint32_t)pmm_alloc(), PTE_U | PTE_P | PTE_R); // �û�����ռ�
    if (vmm_ismap(USER_BASE, &pa))
    {
        for (uint32_t i = 0; i < text.size(); ++i)
        {
            *((int*)pa + i) = text[i];
        }
    }
    /* ӳ��4KB�����ݿռ� */
    vmm_map(DATA_BASE, (uint32_t)pmm_alloc(), PTE_U | PTE_P | PTE_R); // �û����ݿռ�
    if (vmm_ismap(DATA_BASE, &pa))
    {
        for (uint32_t i = 0; i < data.size(); ++i)
        {
            *((char*)pa + i) = data[i];
        }
    }
    /* ӳ��4KB��ջ�ռ� */
    vmm_map(STACK_BASE, (uint32_t)pmm_alloc(), PTE_U | PTE_P | PTE_R); // �û�ջ�ռ�
}

CVirtualMachine::~CVirtualMachine()
{
    free(pgd_kern);
    free(pte_kern);
}

void CVirtualMachine::exec(int entry)
{
    auto poolsize = PAGE_SIZE;
    auto stack = STACK_BASE;
    auto data = DATA_BASE;

    auto sp = stack + poolsize; // 4KB / sizeof(int) = 1024
    vmm_set(--sp, -1);

    auto pc = USER_BASE + entry;
    auto ax = 0;
    auto bp = 0;

    auto cycle = 0;
    while (pc != -1)
    {
        cycle++;
        auto op = vmm_get(pc++); // get next operation code

        assert(op <= PRF);
        // print debug info
        if (false)
        {
            printf("%03d> [%08x] %.4s", cycle, pc,
                &"LEA ,IMM ,JMP ,CALL,JZ  ,JNZ ,ENT ,ADJ ,LEV ,LI  ,LC  ,SI  ,SC  ,PUSH,"
                "OR  ,XOR ,AND ,EQ  ,NE  ,LT  ,GT  ,LE  ,GE  ,SHL ,SHR ,ADD ,SUB ,MUL ,DIV ,MOD ,"
                "PRTF"[op * 5]);
            if (op <= ADJ)
                printf(" %d\n", vmm_get(pc));
            else
                printf("\n");
        }
        if (op == IMM)
        {
            ax = vmm_get(pc++);
        } /* load immediate value to ax */
        else if (op == LC)
        {
            ax = vmm_get<char>(ax);
        } /* load character to ax, address in ax */
        else if (op == LI)
        {
            ax = vmm_get(ax);
        } /* load integer to ax, address in ax */
        else if (op == SC)
        {
            ax = vmm_set<char>(vmm_get(sp++), ax);
        } /* save character to address, value in ax, address on stack */
        else if (op == SI)
        {
            vmm_set(vmm_get(sp++), ax);
        } /* save integer to address, value in ax, address on stack */
        else if (op == PUSH)
        {
            vmm_set(--sp, ax);
        } /* push the value of ax onto the stack */
        else if (op == JMP)
        {
            pc = USER_BASE + vmm_get(pc);
        } /* jump to the address */
        else if (op == JZ)
        {
            pc = ax ? pc + 1 : (USER_BASE + vmm_get(pc));
        } /* jump if ax is zero */
        else if (op == JNZ)
        {
            pc = ax ? (USER_BASE + vmm_get(pc)) : pc + 1;
        } /* jump if ax is zero */
        else if (op == CALL)
        {
            vmm_set(--sp, pc + 1);
            pc = USER_BASE + vmm_get(pc);
        } /* call subroutine */
          /* else if (op == RET) {pc = (int *)*sp++;} // return from subroutine; */
        else if (op == ENT)
        {
            vmm_set(--sp, bp);
            bp = sp;
            sp = sp - vmm_get(pc++);
        } /* make new stack frame */
        else if (op == ADJ)
        {
            sp = sp + vmm_get(pc++);
        } /* add esp, <size> */
        else if (op == LEV)
        {
            sp = bp;
            bp = vmm_get(sp++);
            pc = vmm_get(sp++);
        } /* restore call frame and PC */
        else if (op == LEA)
        {
            ax = bp + vmm_get(pc++);
        } /* load address for arguments. */
        else if (op == PRF)
        {
            auto tmp = sp + vmm_get(pc + 1); /* ����֮���ADJ��ջָ��֪���������õĲ������� */
            auto fmt = vmm_get(tmp-1);
            ax = printf((char*)vmm_getstr(DATA_BASE + fmt), vmm_get(tmp-2), vmm_get(tmp-3), vmm_get(tmp-4), vmm_get(tmp-5), vmm_get(tmp-6));
        } /* load address for arguments. */
        else if (op == OR)
            ax = vmm_get(sp++) | ax;
        else if (op == XOR)
            ax = vmm_get(sp++) ^ ax;
        else if (op == AND)
            ax = vmm_get(sp++) & ax;
        else if (op == EQ)
            ax = vmm_get(sp++) == ax;
        else if (op == NE)
            ax = vmm_get(sp++) != ax;
        else if (op == LT)
            ax = vmm_get(sp++) < ax;
        else if (op == LE)
            ax = vmm_get(sp++) <= ax;
        else if (op == GT)
            ax = vmm_get(sp++) > ax;
        else if (op == GE)
            ax = vmm_get(sp++) >= ax;
        else if (op == SHL)
            ax = vmm_get(sp++) << ax;
        else if (op == SHR)
            ax = vmm_get(sp++) >> ax;
        else if (op == ADD)
            ax = vmm_get(sp++) + ax;
        else if (op == SUB)
            ax = vmm_get(sp++) - ax;
        else if (op == MUL)
            ax = vmm_get(sp++) * ax;
        else if (op == DIV)
            ax = vmm_get(sp++) / ax;
        else if (op == MOD)
            ax = vmm_get(sp++) % ax;
        else
        {
            printf("unknown instruction:%d\n", op);
            assert(0);
        }
    }
}
