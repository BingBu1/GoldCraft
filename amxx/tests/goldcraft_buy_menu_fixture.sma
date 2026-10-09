// TEST ONLY. Compile the actual buy-menu implementation with Bot auto-buy
// disabled to exercise the human menu branch on a headless fakeclient.
// No authentication or production API is changed. Real client input remains
// a separate acceptance test. Never install this variant on the main server.
#include <amxmodx>
stock gc_menu_fixture_is_bot(const id)
{
    #pragma unused id
    return false;
}
#define is_user_bot(%1) gc_menu_fixture_is_bot(%1)
#include "../zombie_plague/menus/zp50_buy_menus.sma"
