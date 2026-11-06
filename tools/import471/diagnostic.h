/* Maintenance-only retained crash evidence. No catalog, audio or radio writes. */
static __noinit volatile struct {uint32_t magic,step,reason,pc,cfsr,hfsr,sp,psp;} diagnostic471;
#define DIAG471 __attribute__((section(".conv469.text.diag471")))
void DIAG471 editor471_checkpoint(unsigned step){
 if(diagnostic471.magic!=0x471d1a60u||step==100){diagnostic471.reason=UINT32_MAX;diagnostic471.pc=diagnostic471.cfsr=diagnostic471.hfsr=0;}
 diagnostic471.step=step;diagnostic471.magic=0x471d1a60u;diagnostic471.sp=__get_MSP();diagnostic471.psp=__get_PSP();
}
void DIAG471 k_sys_fatal_error_handler(unsigned reason,const struct arch_esf*esf){
 diagnostic471.magic=0x471d1a60u;diagnostic471.reason=reason;diagnostic471.pc=esf?esf->basic.pc:0;diagnostic471.cfsr=SCB->CFSR;diagnostic471.hfsr=SCB->HFSR;diagnostic471.sp=__get_MSP();diagnostic471.psp=__get_PSP();__DSB();NVIC_SystemReset();CODE_UNREACHABLE;
}
static const char diagnostic471_format[] __attribute__((section(".conv469.rodata.diag471")))="IMPORT471 DIAG valid=%u step=%u reason=%08x pc=%08x cfsr=%08x hfsr=%08x sp=%08x psp=%08x reset=%08x\n";
static void DIAG471 diagnostic471_report(void){reply(diagnostic471_format,diagnostic471.magic==0x471d1a60u,diagnostic471.step,diagnostic471.reason,diagnostic471.pc,diagnostic471.cfsr,diagnostic471.hfsr,diagnostic471.sp,diagnostic471.psp,reset_reason);}

static const char stack472_format[] __attribute__((section(".conv469.rodata.diag471")))="IMPORT472 STACK main_bytes=%u unused_bytes=%u result=%d\n";
static void DIAG471 maintenance472_stack_report(void){size_t unused=0;int result=k_thread_stack_space_get(k_current_get(),&unused);reply(stack472_format,CONFIG_MAIN_STACK_SIZE,(unsigned)unused,result);}
