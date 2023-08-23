#include "log.h"

pte_t *log_get_pte(struct mm_struct *mm, struct vm_area_struct *vma, unsigned long address)
{
    pgd_t *pgd;
    pud_t *pud;
    pmd_t *pmd;

    pgd = pgd_offset(mm, address);
    if (!pgd_present(*pgd))
        return NULL;

    pud = pud_offset(pgd, address);
    if (!pud_present(*pud))
        return NULL;

    pmd = pmd_offset(pud, address);
    if (!pmd_present(*pmd))
        return NULL;

    return pte_offset_map(pmd, address);
}


void *log_check_addr(struct mm_struct *mm, unsigned long address)
{
    pte_t *ptep;
    struct vm_area_struct *vma = find_vma(mm, address);

    if (!vma) {
        log_err("failed to check address");
        return NULL;
    }

    ptep = log_get_pte(mm, vma, address);
    if (!ptep) {
        int ret;

        ret = handle_mm_fault(mm, vma, address, 0);
        if (ret & VM_FAULT_ERROR) {
            log_err("failed to check address");
            return NULL;
        }
        ptep = log_get_pte(mm, vma, address);
        if (!ptep) {
            log_err("failed to check address");
            return NULL;
        }
    }
    pte_unmap(ptep);
    return page_address(pte_page(*ptep)) + (address & ~PAGE_MASK);
}


void log_regs(struct pt_regs *regs)
{
    if (!regs)
        return;

    printk("pt_regs: ax=%08lx bx=%08lx cx=%08lx dx=%08lx\n",
                regs->ax, regs->bx, regs->cx, regs->dx);
    printk("pt_regs: si=%08lx di=%08lx sp=%08lx bp=%08lx\n",
                regs->si, regs->di, regs->sp, regs->bp);
    printk("pt_regs: ip=%08lx fl=%08lx cs=%08lx ss=%08lx\n",
                regs->ip, regs->flags, regs->cs, regs->ss);
    printk("pt_regs: ds=%08lx es=%08lx fs=%08lx gs=%08lx\n",
                regs->ds, regs->es, regs->fs, regs->gs);
    printk("pt_regs: orig_ax=%08lx\n", regs->orig_ax);
}


void log_task_regs(struct task_struct *tsk)
{
    struct pt_regs *regs = task_pt_regs(tsk);

    log_regs(regs);
}


void log_stack(struct mm_struct *mm, struct pt_regs *regs, loff_t len)
{
    int i, j;

    len = len >> 2 << 2;
    for (i = 0; i < len; i += 4) {
        u32 *ptr = (u32 *)log_check_addr(mm, regs->sp + i);
        printk("sp [%08lx-%08lx]:", regs->sp + i, regs->sp + i + 3);
        for (j = 0; j < 4; j++)
            printk(" %08lx", (unsigned long)ptr[j]);
        printk("\n");
    }
}
