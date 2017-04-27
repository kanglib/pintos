#include "vm/page.h"
#include "vm/swap.h"

void page_init(void)
{
  swap_init();
}
