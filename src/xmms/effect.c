/**
 * @file
 * 
 */


#include <glib.h>

#include "effect.h"
#include "plugin.h"
#include "plugin_int.h"

struct xmms_effect_St {
	struct xmms_effect_St *next;
	void (*deinit) (xmms_effect_t *);
	void (*samplerate_change) (xmms_effect_t *, guint rate);
	void (*run) (xmms_effect_t *, gchar *buf, guint len);
	gpointer *plugin_data;
	guint rate;
};

void
xmms_effect_samplerate_set (xmms_effect_t *effects, guint rate)
{

	for (; effects; effects = effects->next) {
		if (rate != effects->rate) {
			effects->rate = rate;
			effects->samplerate_change (effects, rate);
		}
	}
}

void
xmms_effect_run (xmms_effect_t *effects, gchar *buf, guint len)
{
	for (; effects; effects = effects->next) {
		effects->run (effects, buf, len);
	}
}

/**
 * Retreive plugin-private data.
 *
 * @param effect 
 * @returns the data
 */
gpointer
xmms_effect_plugin_data_get (xmms_effect_t *effect)
{
	gpointer ret;
	g_return_val_if_fail (effect, NULL);

	ret = effect->plugin_data;

	return ret;
}

/**
 * Set plugin-private data.
 *
 * @param effect
 * @param data
 */
void
xmms_effect_plugin_data_set (xmms_effect_t *effect, gpointer data)
{
	g_return_if_fail (effect);

	effect->plugin_data = data;
}


/**
 *
 * @internal
 */
xmms_effect_t *
xmms_effect_prepend (xmms_effect_t *stack, gchar *effect)
{
	GList *list;
	xmms_plugin_t *plugin = NULL;

	g_return_val_if_fail (effect, NULL);

	list = xmms_plugin_list_get (XMMS_PLUGIN_TYPE_EFFECT);

	while (list) {
		plugin = (xmms_plugin_t*) list->data;
		if (!g_strcasecmp (xmms_plugin_shortname_get (plugin), effect)) {
			break;
		}
		list = g_list_next (list);
	}
	if (plugin) {
		void (*initfunc) (xmms_effect_t *);
		xmms_effect_t *effect = g_new0 (xmms_effect_t, 1);

		g_return_val_if_fail (effect, stack);

		initfunc = xmms_plugin_method_get (plugin, 
						   XMMS_PLUGIN_METHOD_INIT);
		
		g_return_val_if_fail (initfunc, stack);

		initfunc (effect);

		effect->samplerate_change = xmms_plugin_method_get (plugin, 
						   XMMS_PLUGIN_METHOD_SAMPLERATE_SET);

		effect->run = xmms_plugin_method_get (plugin, 
						      XMMS_PLUGIN_METHOD_PROCESS);

		effect->deinit = xmms_plugin_method_get (plugin, 
							 XMMS_PLUGIN_METHOD_DEINIT);

		effect->next = stack;
		stack = effect;

	}
	return stack;
}
