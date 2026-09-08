// GENERATED FILE - do not edit.
// Regenerate with: python scripts/gen_imports.py
//
// botw.pouch v1.1, 29 symbol(s), from the surface's own table.
//
// Declaring a symbol here costs nothing. BINDING one is what makes it an
// import, and that is opt-in:
//
//     namespace S { WXL_USE_botw_pouch(SupportsPouch); }
//     S::SupportsPouch(...);
//
// so a mod that uses two symbols imports two, not all 29.
#pragma once

#include <cstdint>

extern "C" {
extern uint32_t wiixl_import__botw_pouch__SupportsPouch(void);
extern int32_t wiixl_import__botw_pouch__SlotCount(int32_t slot);
extern uint32_t wiixl_import__botw_pouch__ItemName(int32_t slot, int32_t index, char* out, uint32_t cap);
extern uint32_t wiixl_import__botw_pouch__ItemValue(int32_t slot, int32_t index, int32_t* out);
extern uint32_t wiixl_import__botw_pouch__ItemPouchIndex(int32_t slot, int32_t index, int32_t* out);
extern uint32_t wiixl_import__botw_pouch__ItemIsEquipped(int32_t slot, int32_t index, uint32_t* out);
extern uint32_t wiixl_import__botw_pouch__ItemIsFood(int32_t slot, int32_t index, uint32_t* out);
extern uint32_t wiixl_import__botw_pouch__ItemModifier(int32_t slot, int32_t index, uint32_t* flags, int32_t* value);
extern uint32_t wiixl_import__botw_pouch__ItemCookData(int32_t slot, int32_t index, int32_t* health, int32_t* duration, int32_t* sellPrice, float* effectId, float* effectLevel);
extern uint32_t wiixl_import__botw_pouch__GetEquippedName(int32_t slot, char* out, uint32_t cap);
extern uint32_t wiixl_import__botw_pouch__GetEquippedValue(int32_t slot, int32_t* out);
extern uint32_t wiixl_import__botw_pouch__SetEquippedValue(int32_t slot, int32_t value);
extern uint32_t wiixl_import__botw_pouch__HasMeaningfulValue(int32_t slot);
extern uint32_t wiixl_import__botw_pouch__IsWeaponSlot(int32_t slot);
extern uint32_t wiixl_import__botw_pouch__AddItem(const char* name, int32_t value);
extern int32_t wiixl_import__botw_pouch__RemoveItem(const char* name, int32_t count);
extern uint32_t wiixl_import__botw_pouch__EquipItem(const char* name);
extern int32_t wiixl_import__botw_pouch__GetTypeForName(const char* name);
extern uint32_t wiixl_import__botw_pouch__SetCookData(int32_t slot, int32_t index, int32_t health, int32_t duration, int32_t sellPrice, float effectId, float effectLevel);
extern uint32_t wiixl_import__botw_pouch__SetItemModifier(int32_t slot, int32_t index, uint32_t flags, int32_t value);
extern uint32_t wiixl_import__botw_pouch__SetModifierByName(const char* name, uint32_t flags, int32_t value);
extern int32_t wiixl_import__botw_pouch__FindItemIndex(int32_t slot, const char* name);
extern uint32_t wiixl_import__botw_pouch__ItemStacks(int32_t slot, int32_t index, uint32_t* out);
extern uint32_t wiixl_import__botw_pouch__CookIngredient(int32_t slot, int32_t index, int32_t ingredientSlot, char* out, uint32_t cap);
extern uint32_t wiixl_import__botw_pouch__IsEquippableSlot(int32_t slot);
extern uint32_t wiixl_import__botw_pouch__CookEffectName(int32_t id, char* out, uint32_t cap);
extern int32_t wiixl_import__botw_pouch__CookEffectFromName(const char* name);
extern uint32_t wiixl_import__botw_pouch__ModifierFromName(const char* name);
extern void wiixl_import__botw_pouch__TickEquipRefresh(void);
}

// The version this header was generated from. A mod that needs a symbol
// added in a later minor should pass --require botw.pouch@1.1 when packing,
// so an older host refuses it by name instead of resolving short.
namespace wiixl_surface_botw_pouch {
inline constexpr unsigned kVersionMajor = 1;
inline constexpr unsigned kVersionMinor = 1;
}

// VOLATILE is not style. Without it the compiler folds the indirect call
// into a direct branch and emits a relocation kind that cannot reach a host
// address - the module fails to relocate. See docs/modules.md.
#define WXL_USE_botw_pouch(sym) \
    inline decltype(&wiixl_import__botw_pouch__##sym) volatile sym = \
        &wiixl_import__botw_pouch__##sym
