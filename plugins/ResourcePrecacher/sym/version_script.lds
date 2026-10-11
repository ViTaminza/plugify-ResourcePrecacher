{
    global:
        Plugify_*;

        /* s2sdk binding slots (defined by s2sdk_bindings, filled by the plugify module) */
        __s2sdk_*;

        /* hudmenu binding slots (defined by hudmenu_bindings, filled by the plugify module from
           the hudmenu plugin's exported GetClientMenuType/SetClientMenuType) */
        __hudmenu_*;

        /* No exported methods: the original's IResourcePrecacher interface (AddPrecache) is not
           exported - Plugify plugins call s2sdk::Precache() directly, which is the same list. */

    local: *;
};
