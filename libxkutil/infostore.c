/*
 * Copyright IBM Corp. 2008
 *
 * Authors:
 *  Dan Smith <danms@us.ibm.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <string.h>

#include <libvirt/libvirt.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xmlsave.h>
#include <libxml/tree.h>

#include <libcmpiutil/libcmpiutil.h>
#include <config.h>

#include "infostore.h"

struct infostore_ctx {
        xmlDocPtr doc;
        xmlNodePtr root;
        xmlXPathContextPtr xpathctx;
        int fd;
};

static void infostore_cleanup_ctx(struct infostore_ctx *ctx)
{
        xmlXPathFreeContext(ctx->xpathctx);
        xmlFreeDoc(ctx->doc);

        if (ctx->fd >= 0)
                close(ctx->fd);

        free(ctx);
}

static char *_make_filename(const char *type, const char *name)
{
        int ret;
        char *path;

        ret = asprintf(&path, "%s/%s_%s",
                       INFO_STORE,
                       type,
                       name);
        if (ret == -1) {
                CU_DEBUG("Failed to asprintf() path to info store");
                path = NULL;
        }

        return path;
}

static char *make_filename(virDomainPtr dom)
{
        virConnectPtr conn;
        char *path = NULL;

        conn = virDomainGetConnect(dom);
        if (conn == NULL) {
                CU_DEBUG("Domain has no libvirt connection");
                goto out;
        }

        path = _make_filename(virConnectGetType(conn),
                              virDomainGetName(dom));

        CU_DEBUG("Path is %s", path);

 out:
        return path;
}

static xmlDocPtr parse_xml(int fd)
{
        xmlParserCtxtPtr ctx = NULL;
        xmlDocPtr doc = NULL;

        ctx = xmlNewParserCtxt();
        if (ctx == NULL)
                goto err;

        doc = xmlCtxtReadFd(ctx,
                            fd,
                            "foo",
                            NULL,
                            XML_PARSE_NOWARNING | XML_PARSE_NONET);
        if (doc == NULL)
                goto err;

        xmlFreeParserCtxt(ctx);

        return doc;
 err:
        xmlFreeDoc(doc);
        xmlFreeParserCtxt(ctx);

        return NULL;
}

static xmlDocPtr new_xml(void)
{
        xmlDocPtr doc = NULL;
        xmlNodePtr root = NULL;

        doc = xmlNewDoc(BAD_CAST "1.0");
        if (doc == NULL) {
                CU_DEBUG("Failed to create new XML document");
                goto err;
        }

        root = xmlNewNode(NULL, BAD_CAST "dominfo");
        if (root == NULL) {
                CU_DEBUG("Failed top create new root node");
                goto err;
        }

        xmlDocSetRootElement(doc, root);

        return doc;
 err:
        xmlFreeDoc(doc);

        return NULL;
}

static bool save_xml(struct infostore_ctx *ctx)
{
        xmlSaveCtxtPtr save = NULL;
        long size = 0;

        lseek(ctx->fd, 0, SEEK_SET);

        if (ftruncate(ctx->fd, 0) != 0)
                CU_DEBUG("Unable to truncate infostore");

        save = xmlSaveToFd(ctx->fd, NULL, 0);
        if (save == NULL) {
                CU_DEBUG("Failed to allocate save context");
                goto out;
        }

        size = xmlSaveDoc(save, ctx->doc);

        xmlSaveClose(save);

 out:
        return size >= 0;
}

static struct infostore_ctx *_generic_infostore_open(char *filename)
{
        struct infostore_ctx *isc;
        struct stat s;

        isc = calloc(1, sizeof(*isc));
        if (isc == NULL) {
                CU_DEBUG("Unable to allocate domain_details struct");
                return NULL;
        }

        isc->fd = open(filename, O_RDWR|O_CREAT, 0600);
        if (isc->fd < 0) {
                CU_DEBUG("Unable to open `%s': %m", filename);
                goto err;
        }

        if (flock(isc->fd, LOCK_EX) != 0) {
                CU_DEBUG("Failed to lock infostore");
                goto err;
        }

        if (fstat(isc->fd, &s) < 0) {
                CU_DEBUG("Failed to fstat infostore");
                goto err;
        }
        if (s.st_size == 0)
                isc->doc = new_xml();
        else
                isc->doc = parse_xml(isc->fd);

        if (isc->doc == NULL) {
                CU_DEBUG("Failed to parse XML");
                goto err;
        }

        isc->root = xmlDocGetRootElement(isc->doc);
        if (isc->root == NULL) {
                CU_DEBUG("Failed to parse XML");
                goto err;
        }

        return isc;

 err:
        infostore_cleanup_ctx(isc);

        return NULL;
}

static struct infostore_ctx *_infostore_open(virDomainPtr dom)
{
        struct infostore_ctx *isc = NULL;
        char *filename = NULL;

        filename = make_filename(dom);
        if (filename == NULL)
                return NULL;

        isc = _generic_infostore_open(filename);
        if (isc == NULL)
                return NULL;

        if (!xmlStrEqual(isc->root->name, BAD_CAST "dominfo")) {
                CU_DEBUG("XML does not start with <dominfo>");
                goto err;
        }

        isc->xpathctx = xmlXPathNewContext(isc->doc);
        if (isc->xpathctx == NULL) {
                CU_DEBUG("Failed to allocate XPath context");
                goto err;
        }

        free(filename);

        return isc;

 err:
        infostore_cleanup_ctx(isc);
        free(filename);

        return NULL;
}

static struct infostore_ctx *delete_and_open(virDomainPtr dom)
{
        char *filename = NULL;

        filename = make_filename(dom);
        if (filename == NULL) {
                CU_DEBUG("Failed to make filename for domain");
                return NULL;
        }

        if (unlink(filename) != 0) {
                CU_DEBUG("Unable to delete %s: %m", filename);
        } else {
                CU_DEBUG("Deleted %s", filename);
        }

        free(filename);

        return _infostore_open(dom);
}

struct infostore_ctx *infostore_open(virDomainPtr dom)
{
        struct infostore_ctx *isc;
        char uuid[VIR_UUID_STRING_BUFLEN];
        char *_uuid = NULL;

        isc = _infostore_open(dom);
        if (isc == NULL)
                return NULL;

        if (virDomainGetUUIDString(dom, uuid) != 0) {
                CU_DEBUG("Failed to get UUID string for comparison");
                infostore_close(isc);
                isc = delete_and_open(dom);
                return isc;
        }

        _uuid = infostore_get_str(isc, "uuid");
        if (_uuid == NULL)
                goto out;

        if (!STREQ(uuid, _uuid)) {
                infostore_close(isc);
                isc = delete_and_open(dom);
        }
 out:
        free(_uuid);
        infostore_set_str(isc, "uuid", uuid);
        return isc;
}

static void _infostore_close(struct infostore_ctx *ctx)
{
        if (ctx == NULL)
                return;

        save_xml(ctx);
        infostore_cleanup_ctx(ctx);
}

void infostore_close(struct infostore_ctx *ctx)
{
        _infostore_close(ctx);
}

void infostore_delete(const char *type, const char *name)
{
        char *path = NULL;

        path = _make_filename(type, name);
        if (path == NULL)
                return;

        unlink(path);

        free(path);
}

static xmlXPathObjectPtr xpath_query(struct infostore_ctx *ctx, const char *key)
{
        char *path = NULL;
        xmlXPathObjectPtr result = NULL;

        if (asprintf(&path, "/dominfo/%s[1]", key) == -1) {
                CU_DEBUG("Failed to alloc path string");
                goto out;
        }

        result = xmlXPathEval(BAD_CAST path, ctx->xpathctx);
        if ((result->type != XPATH_NODESET) ||
            (xmlXPathNodeSetGetLength(result->nodesetval) < 1)) {
                xmlXPathFreeObject(result);
                result = NULL;
        }
 out:
        free(path);

        return result;
}

static char *xpath_query_string(struct infostore_ctx *ctx, const char *key)
{
        xmlXPathObjectPtr result;
        char *val = NULL;

        result = xpath_query(ctx, key);
        if (result != NULL)
                val = (char *)xmlXPathCastToString(result);

        xmlXPathFreeObject(result);

        return val;
}

static bool xpath_set_string(struct infostore_ctx *ctx,
                             const char *key,
                             const char *val)
{
        xmlXPathObjectPtr result = NULL;
        xmlNodePtr node = NULL;

        result = xpath_query(ctx, key);
        if (result == NULL) {
                CU_DEBUG("Creating new node %s=%s", key, val);
                node = xmlNewDocNode(ctx->doc, NULL, BAD_CAST key, NULL);
                xmlAddChild(ctx->root, node);
        } else {
                node = result->nodesetval->nodeTab[0];
        }

        if (node == NULL) {
                CU_DEBUG("Failed to update node for `%s'", key);
                goto out;
        }

        xmlNodeSetContent(node, BAD_CAST val);
 out:
        xmlXPathFreeObject(result);

        return node != NULL;
}

uint64_t infostore_get_u64(struct infostore_ctx *ctx, const char *key)
{
        char *sval = NULL;
        uint64_t val = 0;

        sval = xpath_query_string(ctx, key);
        if (sval == NULL)
                goto out;

        if (sscanf((const char *)sval, "%" SCNu64, &val) != 1) {
                CU_DEBUG("Failed to parse u64 for %s (%s)", key, sval);
                goto out;
        }
 out:
        free(sval);

        return val;
}

bool infostore_set_u64(struct infostore_ctx *ctx, const char *key, uint64_t val)
{
        char *sval = NULL;

        if (asprintf(&sval, "%" PRIu64, val) == -1) {
                CU_DEBUG("Failed to format u64 string");
                sval = NULL;
                goto out;
        }

        xpath_set_string(ctx, key, sval);
 out:
        free(sval);

        return true;
}

char *infostore_get_str(struct infostore_ctx *ctx, const char *key)
{
        return xpath_query_string(ctx, key);
}

bool infostore_set_str(struct infostore_ctx *ctx,
                       const char *key, const char * val)
{
        return xpath_set_string(ctx, key, val);
}

bool infostore_get_bool(struct infostore_ctx *ctx, const char *key)
{
        char *sval = NULL;
        bool val = false;

        sval = xpath_query_string(ctx, key);
        if (sval == NULL)
                goto out;

        if (STREQC(sval, "true"))
                return true;

 out:
        free(sval);

        return val;
}

bool infostore_set_bool(struct infostore_ctx *ctx, const char *key, bool val)
{
        bool ret;

        if (val)
                ret = xpath_set_string(ctx, key, "true");
        else
                ret = xpath_set_string(ctx, key, "false");

        return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-set-style: "K&R"
 * tab-width: 8
 * c-basic-offset: 8
 * indent-tabs-mode: nil
 * End:
 */
