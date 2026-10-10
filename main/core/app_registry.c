/*
 * The list of apps shown on the home screen. See app_registry.h for how to add one.
 */
#include "apps/gallery/gallery_app.h"
#include "apps/notes/notes_app.h"
#include "apps/settings/settings_app.h"
#include "apps/watch/watch_app.h"
#include "apps/weather/weather_app.h"
#include "app_registry.h"

// Home screen order
static const app_desc_t *const apps[] = {
    &gallery_app,
    &watch_app,
    &notes_app,
    &weather_app,
    &settings_app,
};

size_t app_registry_count(void)
{
    return sizeof(apps) / sizeof(apps[0]);
}

const app_desc_t *app_registry_get(size_t index)
{
    return index < app_registry_count() ? apps[index] : NULL;
}
