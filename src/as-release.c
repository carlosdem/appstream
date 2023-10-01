/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2014-2023 Matthias Klumpp <matthias@tenstral.net>
 * Copyright (C) 2014 Richard Hughes <richard@hughsie.com>
 *
 * Licensed under the GNU Lesser General Public License Version 2.1
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 2.1 of the license, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:as-release
 * @short_description: Object representing a single upstream release
 * @include: appstream.h
 *
 * This object represents a single upstream release, typically a minor update.
 * Releases can contain a localized description of paragraph and list elements
 * and also have a version number and timestamp.
 *
 * Releases can be automatically generated by parsing upstream ChangeLogs or
 * .spec files, or can be populated using MetaInfo files.
 *
 * See also: #AsComponent
 */

#include "as-release-private.h"

#include "as-utils.h"
#include "as-utils-private.h"
#include "as-vercmp.h"
#include "as-context-private.h"
#include "as-artifact-private.h"
#include "as-checksum-private.h"
#include "as-issue-private.h"

typedef struct {
	AsReleaseKind kind;
	gchar *version;
	GHashTable *description;
	guint64 timestamp;
	gchar *date;
	gchar *date_eol;

	AsContext *context;
	gboolean desc_translatable;

	GPtrArray *issues;
	GPtrArray *artifacts;

	gchar *url_details;

	AsUrgencyKind urgency;
} AsReleasePrivate;

G_DEFINE_TYPE_WITH_PRIVATE (AsRelease, as_release, G_TYPE_OBJECT)
#define GET_PRIVATE(o) (as_release_get_instance_private (o))

/**
 * as_release_kind_to_string:
 * @kind: the #AsReleaseKind.
 *
 * Converts the enumerated value to an text representation.
 *
 * Returns: string version of @kind
 *
 * Since: 0.12.0
 **/
const gchar *
as_release_kind_to_string (AsReleaseKind kind)
{
	if (kind == AS_RELEASE_KIND_STABLE)
		return "stable";
	if (kind == AS_RELEASE_KIND_DEVELOPMENT)
		return "development";
	return "unknown";
}

/**
 * as_release_kind_from_string:
 * @kind_str: the string.
 *
 * Converts the text representation to an enumerated value.
 *
 * Returns: an #AsReleaseKind or %AS_RELEASE_KIND_UNKNOWN for unknown
 *
 * Since: 0.12.0
 **/
AsReleaseKind
as_release_kind_from_string (const gchar *kind_str)
{
	if (g_strcmp0 (kind_str, "stable") == 0)
		return AS_RELEASE_KIND_STABLE;
	if (g_strcmp0 (kind_str, "development") == 0)
		return AS_RELEASE_KIND_DEVELOPMENT;
	return AS_RELEASE_KIND_UNKNOWN;
}

/**
 * as_urgency_kind_to_string:
 * @urgency_kind: the %AsUrgencyKind.
 *
 * Converts the enumerated value to an text representation.
 *
 * Returns: string version of @urgency_kind
 *
 * Since: 0.6.5
 **/
const gchar *
as_urgency_kind_to_string (AsUrgencyKind urgency_kind)
{
	if (urgency_kind == AS_URGENCY_KIND_LOW)
		return "low";
	if (urgency_kind == AS_URGENCY_KIND_MEDIUM)
		return "medium";
	if (urgency_kind == AS_URGENCY_KIND_HIGH)
		return "high";
	if (urgency_kind == AS_URGENCY_KIND_CRITICAL)
		return "critical";
	return "unknown";
}

/**
 * as_urgency_kind_from_string:
 * @urgency_kind: the string.
 *
 * Converts the text representation to an enumerated value.
 *
 * Returns: a %AsUrgencyKind or %AS_URGENCY_KIND_UNKNOWN for unknown
 *
 * Since: 0.6.5
 **/
AsUrgencyKind
as_urgency_kind_from_string (const gchar *urgency_kind)
{
	if (g_strcmp0 (urgency_kind, "low") == 0)
		return AS_URGENCY_KIND_LOW;
	if (g_strcmp0 (urgency_kind, "medium") == 0)
		return AS_URGENCY_KIND_MEDIUM;
	if (g_strcmp0 (urgency_kind, "high") == 0)
		return AS_URGENCY_KIND_HIGH;
	if (g_strcmp0 (urgency_kind, "critical") == 0)
		return AS_URGENCY_KIND_CRITICAL;
	return AS_URGENCY_KIND_UNKNOWN;
}

/**
 * as_release_url_kind_to_string:
 * @kind: the #AsReleaseUrlKind.
 *
 * Converts the enumerated value to an text representation.
 *
 * Returns: string version of @kind
 *
 * Since: 0.12.5
 **/
const gchar *
as_release_url_kind_to_string (AsReleaseUrlKind kind)
{
	if (kind == AS_RELEASE_URL_KIND_DETAILS)
		return "details";
	return "unknown";
}

/**
 * as_release_url_kind_from_string:
 * @kind_str: the string.
 *
 * Converts the text representation to an enumerated value.
 *
 * Returns: an #AsReleaseUrlKind or %AS_RELEASE_URL_KIND_UNKNOWN for unknown
 *
 * Since: 0.12.5
 **/
AsReleaseUrlKind
as_release_url_kind_from_string (const gchar *kind_str)
{
	if (kind_str == NULL)
		return AS_RELEASE_URL_KIND_DETAILS;
	if (g_strcmp0 (kind_str, "details") == 0)
		return AS_RELEASE_URL_KIND_DETAILS;
	return AS_RELEASE_URL_KIND_UNKNOWN;
}

/**
 * as_release_init:
 **/
static void
as_release_init (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);

	/* we assume a stable release by default */
	priv->kind = AS_RELEASE_KIND_STABLE;

	priv->description = g_hash_table_new_full (g_str_hash,
						   g_str_equal,
						   (GDestroyNotify) as_ref_string_release,
						   g_free);
	priv->issues = g_ptr_array_new_with_free_func (g_object_unref);
	priv->artifacts = g_ptr_array_new_with_free_func (g_object_unref);
	priv->urgency = AS_URGENCY_KIND_UNKNOWN;
	priv->desc_translatable = TRUE;
}

/**
 * as_release_finalize:
 **/
static void
as_release_finalize (GObject *object)
{
	AsRelease *release = AS_RELEASE (object);
	AsReleasePrivate *priv = GET_PRIVATE (release);

	g_free (priv->version);
	g_free (priv->date);
	g_free (priv->date_eol);
	g_free (priv->url_details);
	g_hash_table_unref (priv->description);
	g_ptr_array_unref (priv->issues);
	g_ptr_array_unref (priv->artifacts);
	if (priv->context != NULL)
		g_object_unref (priv->context);

	G_OBJECT_CLASS (as_release_parent_class)->finalize (object);
}

/**
 * as_release_class_init:
 **/
static void
as_release_class_init (AsReleaseClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = as_release_finalize;
}

/**
 * as_release_get_kind:
 * @release: a #AsRelease instance.
 *
 * Gets the type of the release.
 * (development or stable release)
 *
 * Since: 0.12.0
 **/
AsReleaseKind
as_release_get_kind (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (AS_IS_RELEASE (release), AS_RELEASE_KIND_UNKNOWN);
	return priv->kind;
}

/**
 * as_release_set_kind:
 * @release: a #AsRelease instance.
 * @kind: the #AsReleaseKind
 *
 * Sets the release kind to distinguish between end-user ready
 * stable releases and development prereleases..
 *
 * Since: 0.12.0
 **/
void
as_release_set_kind (AsRelease *release, AsReleaseKind kind)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (AS_IS_RELEASE (release));
	priv->kind = kind;
}

/**
 * as_release_get_version:
 * @release: a #AsRelease instance.
 *
 * Gets the release version.
 *
 * Returns: (nullable): string, or %NULL for not set or invalid
 **/
const gchar *
as_release_get_version (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (AS_IS_RELEASE (release), NULL);
	return priv->version;
}

/**
 * as_release_set_version:
 * @release: a #AsRelease instance.
 * @version: the version string.
 *
 * Sets the release version.
 **/
void
as_release_set_version (AsRelease *release, const gchar *version)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (AS_IS_RELEASE (release));
	as_assign_string_safe (priv->version, version);
}

/**
 * as_release_vercmp:
 * @rel1: an #AsRelease
 * @rel2: an #AsRelease
 *
 * Compare the version numbers of two releases.
 *
 * Returns: 1 if @rel1 version is higher than @rel2, 0 if versions are equal, -1 if @rel2 version is higher than @rel1.
 */
gint
as_release_vercmp (AsRelease *rel1, AsRelease *rel2)
{
	g_return_val_if_fail (AS_IS_RELEASE (rel1), 0);
	g_return_val_if_fail (AS_IS_RELEASE (rel2), 0);
	return as_vercmp_simple (as_release_get_version (rel1), as_release_get_version (rel2));
}

/**
 * as_release_get_timestamp:
 * @release: a #AsRelease instance.
 *
 * Gets the release timestamp.
 *
 * Returns: timestamp, or 0 for unset
 **/
guint64
as_release_get_timestamp (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (AS_IS_RELEASE (release), 0);
	return priv->timestamp;
}

/**
 * as_release_set_timestamp:
 * @release: a #AsRelease instance.
 * @timestamp: the timestamp value.
 *
 * Sets the release timestamp.
 **/
void
as_release_set_timestamp (AsRelease *release, guint64 timestamp)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_autoptr(GDateTime) time = g_date_time_new_from_unix_utc (timestamp);

	g_return_if_fail (AS_IS_RELEASE (release));

	priv->timestamp = timestamp;
	g_free (priv->date);
	priv->date = g_date_time_format_iso8601 (time);
}

/**
 * as_release_get_date:
 * @release: a #AsRelease instance.
 *
 * Gets the release date.
 *
 * Returns: (nullable): The date in ISO8601 format.
 *
 * Since: 0.12.5
 **/
const gchar *
as_release_get_date (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (AS_IS_RELEASE (release), NULL);
	return priv->date;
}

/**
 * as_release_set_date:
 * @release: a #AsRelease instance.
 * @date: the date in ISO8601 format.
 *
 * Sets the release date.
 *
 * Since: 0.12.5
 **/
void
as_release_set_date (AsRelease *release, const gchar *date)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_autoptr(GDateTime) time = NULL;

	g_return_if_fail (AS_IS_RELEASE (release));
	g_return_if_fail (date != NULL);

	time = as_iso8601_to_datetime (date);
	if (time != NULL) {
		priv->timestamp = g_date_time_to_unix (time);
	} else {
		g_warning ("Tried to set invalid release date: %s", date);
		return;
	}

	as_assign_string_safe (priv->date, date);
}

/**
 * as_release_get_date_eol:
 * @release: a #AsRelease instance.
 *
 * Gets the end-of-life date for this release.
 *
 * Returns: (nullable): The EOL date in ISO8601 format.
 *
 * Since: 0.12.5
 **/
const gchar *
as_release_get_date_eol (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (AS_IS_RELEASE (release), NULL);
	return priv->date_eol;
}

/**
 * as_release_set_date_eol:
 * @release: a #AsRelease instance.
 * @date: the EOL date in ISO8601 format.
 *
 * Sets the end-of-life date for this release.
 *
 * Since: 0.12.5
 **/
void
as_release_set_date_eol (AsRelease *release, const gchar *date)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (AS_IS_RELEASE (release));
	g_return_if_fail (date != NULL);
	as_assign_string_safe (priv->date_eol, date);
}

/**
 * as_release_get_timestamp_eol:
 * @release: a #AsRelease instance.
 *
 * Gets the UNIX timestamp for the date when this
 * release is out of support (end-of-life).
 *
 * Returns: UNIX timestamp, or 0 for unset or invalid.
 *
 * Since: 0.12.5
 **/
guint64
as_release_get_timestamp_eol (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_autoptr(GDateTime) time = NULL;

	g_return_val_if_fail (AS_IS_RELEASE (release), 0);

	if (priv->date_eol == NULL)
		return 0;

	time = as_iso8601_to_datetime (priv->date_eol);
	if (time != NULL) {
		return g_date_time_to_unix (time);
	} else {
		g_warning ("Unable to retrieve EOL timestamp from EOL date: %s", priv->date_eol);
		return 0;
	}
}

/**
 * as_release_set_timestamp_eol:
 * @release: a #AsRelease instance.
 * @timestamp: the timestamp value.
 *
 * Sets the UNIX timestamp for the date when this
 * release is out of support (end-of-life).
 *
 * Since: 0.12.5
 **/
void
as_release_set_timestamp_eol (AsRelease *release, guint64 timestamp)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_autoptr(GDateTime) time = NULL;

	g_return_if_fail (AS_IS_RELEASE (release));

	if (timestamp == 0)
		return;

	time = g_date_time_new_from_unix_utc (timestamp);
	g_free (priv->date_eol);
	priv->date_eol = g_date_time_format_iso8601 (time);
}

/**
 * as_release_get_urgency:
 * @release: a #AsRelease instance.
 *
 * Gets the urgency of the release
 * (showing how important it is to update to a more recent release)
 *
 * Returns: #AsUrgencyKind, or %AS_URGENCY_KIND_UNKNOWN for not set
 *
 * Since: 0.6.5
 **/
AsUrgencyKind
as_release_get_urgency (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (AS_IS_RELEASE (release), AS_URGENCY_KIND_UNKNOWN);
	return priv->urgency;
}

/**
 * as_release_set_urgency:
 * @release: a #AsRelease instance.
 * @urgency: the urgency of this release/update (as #AsUrgencyKind)
 *
 * Sets the release urgency.
 *
 * Since: 0.6.5
 **/
void
as_release_set_urgency (AsRelease *release, AsUrgencyKind urgency)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (AS_IS_RELEASE (release));
	priv->urgency = urgency;
}

/**
 * as_release_get_description:
 * @release: a #AsRelease instance.
 *
 * Gets the release description markup for a given locale.
 *
 * Returns: (nullable): markup, or %NULL for not set or invalid
 **/
const gchar *
as_release_get_description (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (AS_IS_RELEASE (release), NULL);
	return as_context_localized_ht_get (priv->context,
					    priv->description,
					    NULL, /* locale override */
					    AS_VALUE_FLAG_NONE);
}

/**
 * as_release_set_description:
 * @release: a #AsRelease instance.
 * @description: the description markup.
 * @locale: (nullable): the BCP47 locale, or %NULL. e.g. "en-GB".
 *
 * Sets the description release markup.
 **/
void
as_release_set_description (AsRelease *release, const gchar *description, const gchar *locale)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (AS_IS_RELEASE (release));
	g_return_if_fail (description != NULL);
	as_context_localized_ht_set (priv->context, priv->description, description, locale);
}

/**
 * as_release_get_artifacts:
 *
 * Get a list of all downloadable artifacts that are associated with
 * this release.
 *
 * Returns: (transfer none) (element-type AsArtifact): an array of #AsArtifact objects.
 *
 * Since: 0.12.6
 **/
GPtrArray *
as_release_get_artifacts (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (AS_IS_RELEASE (release), NULL);
	return priv->artifacts;
}

/**
 * as_release_add_artifact:
 * @release: An instance of #AsRelease.
 * @artifact: The #AsArtifact.
 *
 * Add an artifact (binary / source download) for this release.
 *
 * Since: 0.12.6
 */
void
as_release_add_artifact (AsRelease *release, AsArtifact *artifact)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);

	g_return_if_fail (AS_IS_RELEASE (release));
	g_return_if_fail (AS_IS_ARTIFACT (artifact));

	g_ptr_array_add (priv->artifacts, g_object_ref (artifact));
}

/**
 * as_release_get_issues:
 *
 * Get a list of all issues resolved by this release.
 *
 * Returns: (transfer none) (element-type AsIssue): an array of #AsIssue objects.
 *
 * Since: 0.12.9
 **/
GPtrArray *
as_release_get_issues (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (AS_IS_RELEASE (release), NULL);
	return priv->issues;
}

/**
 * as_release_add_issue:
 * @release: An instance of #AsRelease.
 * @issue: The #AsIssue.
 *
 * Add information about a (resolved) issue to this release.
 *
 * Since: 0.12.9
 */
void
as_release_add_issue (AsRelease *release, AsIssue *issue)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);

	g_return_if_fail (AS_IS_RELEASE (release));
	g_return_if_fail (AS_IS_ISSUE (issue));

	g_ptr_array_add (priv->issues, g_object_ref (issue));
}

/**
 * as_release_get_url:
 * @release: a #AsRelease instance.
 * @url_kind: the URL kind, e.g. %AS_RELEASE_URL_KIND_DETAILS.
 *
 * Gets an URL.
 *
 * Returns: (nullable): string, or %NULL if unset
 *
 * Since: 0.12.5
 **/
const gchar *
as_release_get_url (AsRelease *release, AsReleaseUrlKind url_kind)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);

	g_return_val_if_fail (AS_IS_RELEASE (release), NULL);

	if (url_kind == AS_RELEASE_URL_KIND_DETAILS)
		return priv->url_details;
	return NULL;
}

/**
 * as_release_set_url:
 * @release: a #AsRelease instance.
 * @url_kind: the URL kind, e.g. %AS_RELEASE_URL_KIND_DETAILS
 * @url: the full URL.
 *
 * Sets an URL for this release.
 *
 * Since: 0.12.5
 **/
void
as_release_set_url (AsRelease *release, AsReleaseUrlKind url_kind, const gchar *url)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);

	g_return_if_fail (AS_IS_RELEASE (release));

	if (url_kind == AS_RELEASE_URL_KIND_DETAILS)
		as_assign_string_safe (priv->url_details, url);
}

/**
 * as_release_get_context:
 * @release: An instance of #AsRelease.
 *
 * Returns: (transfer none) (nullable): the #AsContext associated with this release.
 * This function may return %NULL if no context is set.
 *
 * Since: 0.11.2
 */
AsContext *
as_release_get_context (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (AS_IS_RELEASE (release), NULL);

	return priv->context;
}

/**
 * as_release_set_context:
 * @release: An instance of #AsRelease.
 * @context: the #AsContext.
 *
 * Sets the document context this release is associated
 * with.
 *
 * Since: 0.11.2
 */
void
as_release_set_context (AsRelease *release, AsContext *context)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (AS_IS_RELEASE (release));

	g_set_object (&priv->context, context);
}

/**
 * as_release_description_translatable:
 * @release: a #AsRelease instance.
 *
 * Check if a MetaInfo description for this release is marked
 * for translation by translators.
 *
 * Returns: %TRUE if description can be translated.
 **/
gboolean
as_release_description_translatable (AsRelease *release)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_val_if_fail (AS_IS_RELEASE (release), FALSE);
	return priv->desc_translatable;
}

/**
 * as_release_set_description_translatable:
 * @release: a #AsRelease instance.
 * @translatable: %TRUE if translation is enabled.
 *
 * Sets whether a MetaInfo description for this release is marked
 * for translation.
 **/
void
as_release_set_description_translatable (AsRelease *release, gboolean translatable)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	g_return_if_fail (AS_IS_RELEASE (release));
	priv->desc_translatable = translatable;
}

/**
 * as_release_load_from_xml:
 * @release: an #AsRelease
 * @ctx: the AppStream document context.
 * @node: the XML node.
 * @error: a #GError.
 *
 * Loads data from an XML node.
 **/
gboolean
as_release_load_from_xml (AsRelease *release, AsContext *ctx, xmlNode *node, GError **error)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	xmlNode *iter;
	gchar *prop;

	/* propagate context */
	as_release_set_context (release, ctx);

	prop = as_xml_get_prop_value (node, "type");
	if (prop != NULL) {
		priv->kind = as_release_kind_from_string (prop);
		g_free (prop);
	}

	prop = as_xml_get_prop_value (node, "version");
	as_release_set_version (release, prop);
	g_free (prop);

	prop = as_xml_get_prop_value (node, "date");
	if (prop != NULL) {
		g_autoptr(GDateTime) time = as_iso8601_to_datetime (prop);
		if (time != NULL) {
			priv->timestamp = g_date_time_to_unix (time);
			g_free (priv->date);
			priv->date = prop;
		} else {
			g_debug ("Invalid ISO-8601 date in releases at %s line %li",
				 as_context_get_filename (ctx),
				 xmlGetLineNo (node));
			g_free (prop);
		}
	}

	prop = as_xml_get_prop_value (node, "date_eol");
	if (prop != NULL) {
		g_free (priv->date_eol);
		priv->date_eol = prop;
	}

	prop = as_xml_get_prop_value (node, "timestamp");
	if (prop != NULL) {
		priv->timestamp = atol (prop);
		g_free (prop);
	}
	prop = as_xml_get_prop_value (node, "urgency");
	if (prop != NULL) {
		priv->urgency = as_urgency_kind_from_string (prop);
		g_free (prop);
	}

	for (iter = node->children; iter != NULL; iter = iter->next) {
		g_autofree gchar *content = NULL;
		if (iter->type != XML_ELEMENT_NODE)
			continue;

		if (g_strcmp0 ((gchar *) iter->name, "artifacts") == 0) {
			for (xmlNode *iter2 = iter->children; iter2 != NULL; iter2 = iter2->next) {
				g_autoptr(AsArtifact) artifact = NULL;

				if (iter2->type != XML_ELEMENT_NODE)
					continue;

				artifact = as_artifact_new ();
				if (as_artifact_load_from_xml (artifact, ctx, iter2, NULL))
					as_release_add_artifact (release, artifact);
			}
		} else if (g_strcmp0 ((gchar *) iter->name, "description") == 0) {
			g_hash_table_remove_all (priv->description);
			if (as_context_get_style (ctx) == AS_FORMAT_STYLE_CATALOG) {
				g_autofree gchar *lang = NULL;

				/* for catalog XML, the "description" tag has a language property, so parsing it is simple */
				content = as_xml_dump_node_children (iter);
				lang = as_xml_get_node_locale_match (ctx, iter);
				if (lang != NULL)
					as_release_set_description (release, content, lang);
			} else {
				as_xml_parse_metainfo_description_node (ctx,
									iter,
									priv->description);

				priv->desc_translatable = TRUE;
				prop = as_xml_get_prop_value (iter, "translatable");
				if (prop != NULL) {
					priv->desc_translatable = g_strcmp0 (prop, "no") != 0;
					g_free (prop);
				}
			}
		} else if (g_strcmp0 ((gchar *) iter->name, "url") == 0) {
			/* NOTE: Currently, every url in releases is a "details" URL */
			content = as_xml_get_node_value (iter);
			as_release_set_url (release, AS_RELEASE_URL_KIND_DETAILS, content);
		} else if (g_strcmp0 ((gchar *) iter->name, "issues") == 0) {
			for (xmlNode *iter2 = iter->children; iter2 != NULL; iter2 = iter2->next) {
				g_autoptr(AsIssue) issue = NULL;

				if (iter2->type != XML_ELEMENT_NODE)
					continue;

				issue = as_issue_new ();
				if (as_issue_load_from_xml (issue, ctx, iter2, NULL))
					as_release_add_issue (release, issue);
			}
		}
	}

	return TRUE;
}

/**
 * as_release_to_xml_node:
 * @release: an #AsRelease
 * @ctx: the AppStream document context.
 * @root: XML node to attach the new nodes to.
 *
 * Serializes the data to an XML node.
 **/
void
as_release_to_xml_node (AsRelease *release, AsContext *ctx, xmlNode *root)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);
	xmlNode *subnode;

	/* set release version */
	subnode = as_xml_add_node (root, "release");
	as_xml_add_text_prop (subnode, "type", as_release_kind_to_string (priv->kind));
	as_xml_add_text_prop (subnode, "version", priv->version);

	/* set release timestamp / date */
	if (priv->timestamp > 0) {
		g_autofree gchar *time_str = NULL;

		if (as_context_get_style (ctx) == AS_FORMAT_STYLE_CATALOG) {
			time_str = g_strdup_printf ("%" G_GUINT64_FORMAT, priv->timestamp);
			as_xml_add_text_prop (subnode, "timestamp", time_str);
		} else {
			g_autoptr(GDateTime)
				       time = g_date_time_new_from_unix_utc (priv->timestamp);
			time_str = g_date_time_format_iso8601 (time);
			as_xml_add_text_prop (subnode, "date", time_str);
		}
	}

	/* set end-of-life date */
	if (priv->date_eol != NULL)
		as_xml_add_text_prop (subnode, "date_eol", priv->date_eol);

	/* set release urgency, if we have one */
	if (priv->urgency != AS_URGENCY_KIND_UNKNOWN) {
		const gchar *urgency_str;
		urgency_str = as_urgency_kind_to_string (priv->urgency);
		as_xml_add_text_prop (subnode, "urgency", urgency_str);
	}

	/* add description */
	as_xml_add_description_node (ctx, subnode, priv->description, priv->desc_translatable);

	/* add details URL */
	if (priv->url_details != NULL)
		as_xml_add_text_node (subnode, "url", priv->url_details);

	/* issues */
	if (priv->issues->len > 0) {
		xmlNode *n_issues = as_xml_add_node (subnode, "issues");
		for (guint i = 0; i < priv->issues->len; i++) {
			AsIssue *issue = AS_ISSUE (g_ptr_array_index (priv->issues, i));
			as_issue_to_xml_node (issue, ctx, n_issues);
		}
	}

	/* artifacts */
	if (priv->artifacts->len > 0) {
		xmlNode *n_artifacts = as_xml_add_node (subnode, "artifacts");
		for (guint i = 0; i < priv->artifacts->len; i++) {
			AsArtifact *artifact = AS_ARTIFACT (g_ptr_array_index (priv->artifacts, i));
			as_artifact_to_xml_node (artifact, ctx, n_artifacts);
		}
	}
}

/**
 * as_release_load_from_yaml:
 * @release: an #AsRelease
 * @ctx: the AppStream document context.
 * @node: the YAML node.
 * @error: a #GError.
 *
 * Loads data from a YAML field.
 **/
gboolean
as_release_load_from_yaml (AsRelease *release, AsContext *ctx, GNode *node, GError **error)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);

	/* propagate locale */
	as_release_set_context (release, ctx);

	for (GNode *n = node->children; n != NULL; n = n->next) {
		const gchar *key = as_yaml_node_get_key (n);
		const gchar *value = as_yaml_node_get_value (n);

		if (g_strcmp0 (key, "unix-timestamp") == 0) {
			priv->timestamp = atol (value);
		} else if (g_strcmp0 (key, "date") == 0) {
			g_autoptr(GDateTime) time = as_iso8601_to_datetime (value);
			if (time != NULL) {
				priv->timestamp = g_date_time_to_unix (time);
			} else {
				/* FIXME: Better error, maybe with line number? */
				g_debug ("Invalid ISO-8601 release date in %s",
					 as_context_get_filename (ctx));
			}
		} else if (g_strcmp0 (key, "date-eol") == 0) {
			as_release_set_date_eol (release, value);
		} else if (g_strcmp0 (key, "type") == 0) {
			priv->kind = as_release_kind_from_string (value);
		} else if (g_strcmp0 (key, "version") == 0) {
			as_release_set_version (release, value);
		} else if (g_strcmp0 (key, "urgency") == 0) {
			priv->urgency = as_urgency_kind_from_string (value);
		} else if (g_strcmp0 (key, "description") == 0) {
			as_yaml_set_localized_table (ctx, n, priv->description);
		} else if (g_strcmp0 (key, "url") == 0) {
			GNode *urls_n;
			AsReleaseUrlKind url_kind;

			for (urls_n = n->children; urls_n != NULL; urls_n = urls_n->next) {
				const gchar *c_key = as_yaml_node_get_key (urls_n);
				const gchar *c_value = as_yaml_node_get_value (urls_n);

				url_kind = as_release_url_kind_from_string (c_key);
				if ((url_kind != AS_RELEASE_URL_KIND_UNKNOWN) && (c_value != NULL))
					as_release_set_url (release, url_kind, c_value);
			}

		} else if (g_strcmp0 (key, "issues") == 0) {
			for (GNode *sn = n->children; sn != NULL; sn = sn->next) {
				g_autoptr(AsIssue) issue = as_issue_new ();
				if (as_issue_load_from_yaml (issue, ctx, sn, NULL))
					as_release_add_issue (release, issue);
			}

		} else if (g_strcmp0 (key, "artifacts") == 0) {
			for (GNode *sn = n->children; sn != NULL; sn = sn->next) {
				g_autoptr(AsArtifact) artifact = as_artifact_new ();
				if (as_artifact_load_from_yaml (artifact, ctx, sn, NULL))
					as_release_add_artifact (release, artifact);
			}

		} else {
			as_yaml_print_unknown ("release", key);
		}
	}

	return TRUE;
}

/**
 * as_release_emit_yaml:
 * @release: an #AsRelease
 * @ctx: the AppStream document context.
 * @emitter: The YAML emitter to emit data on.
 *
 * Emit YAML data for this object.
 **/
void
as_release_emit_yaml (AsRelease *release, AsContext *ctx, yaml_emitter_t *emitter)
{
	AsReleasePrivate *priv = GET_PRIVATE (release);

	/* start mapping for this release */
	as_yaml_mapping_start (emitter);

	/* version */
	as_yaml_emit_entry (emitter, "version", priv->version);

	/* type */
	as_yaml_emit_entry (emitter, "type", as_release_kind_to_string (priv->kind));

	/* timestamp & date */
	if (priv->timestamp > 0) {
		g_autofree gchar *time_str = NULL;

		if (as_context_get_style (ctx) == AS_FORMAT_STYLE_CATALOG) {
			as_yaml_emit_entry_timestamp (emitter, "unix-timestamp", priv->timestamp);
		} else {
			g_autoptr(GDateTime)
				       time = g_date_time_new_from_unix_utc (priv->timestamp);
			time_str = g_date_time_format_iso8601 (time);
			as_yaml_emit_entry (emitter, "date", time_str);
		}
	}

	/* EOL date */
	as_yaml_emit_entry (emitter, "date-eol", priv->date_eol);

	/* urgency */
	if (priv->urgency != AS_URGENCY_KIND_UNKNOWN) {
		as_yaml_emit_entry (emitter, "urgency", as_urgency_kind_to_string (priv->urgency));
	}

	/* description */
	as_yaml_emit_long_localized_entry (emitter, "description", priv->description);

	/* urls */
	if (priv->url_details != NULL) {
		as_yaml_emit_scalar (emitter, "url");
		as_yaml_mapping_start (emitter);

		as_yaml_emit_entry (emitter,
				    as_release_url_kind_to_string (AS_RELEASE_URL_KIND_DETAILS),
				    (const gchar *) priv->url_details);

		as_yaml_mapping_end (emitter);
	}

	/* issues */
	if (priv->issues->len > 0) {
		as_yaml_emit_scalar (emitter, "issues");
		as_yaml_sequence_start (emitter);

		for (guint i = 0; i < priv->issues->len; i++) {
			AsIssue *issue = AS_ISSUE (g_ptr_array_index (priv->issues, i));
			as_issue_emit_yaml (issue, ctx, emitter);
		}

		as_yaml_sequence_end (emitter);
	}

	/* artifacts */
	if (priv->artifacts->len > 0) {
		as_yaml_emit_scalar (emitter, "artifacts");
		as_yaml_sequence_start (emitter);

		for (guint i = 0; i < priv->artifacts->len; i++) {
			AsArtifact *artifact = AS_ARTIFACT (g_ptr_array_index (priv->artifacts, i));
			as_artifact_emit_yaml (artifact, ctx, emitter);
		}

		as_yaml_sequence_end (emitter);
	}

	/* end mapping for the release */
	as_yaml_mapping_end (emitter);
}

/**
 * as_release_new:
 *
 * Creates a new #AsRelease.
 *
 * Returns: (transfer full): a #AsRelease
 **/
AsRelease *
as_release_new (void)
{
	AsRelease *release;
	release = g_object_new (AS_TYPE_RELEASE, NULL);
	return AS_RELEASE (release);
}
