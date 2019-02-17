/* Copyright (C) 2009 Trend Micro Inc.
 * All rights reserved.
 *
 * This program is a free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation.
 */

#include "shared.h"
#include "os_regex/os_regex.h"
#include "os_xml/os_xml.h"
#include "eventinfo.h"
#include "decoder.h"


/* Use the osdecoders to decode the received event */
void DecodeEvent(Eventinfo *lf)
{
    OSDecoderNode *node;
    OSDecoderNode *child_node;
    OSDecoderInfo *nnode;

    const char *llog = NULL;
    const char *pmatch = NULL;
    const char *cmatch = NULL;
    const char *regex_prev = NULL;

    node = OS_GetFirstOSDecoder(lf->program_name);

    if (!node) {
        return;
    }

#ifdef TESTRULE
    if (!alert_only) {
        print_out("\n**Phase 2: Completed decoding.");
    }
#endif

    do {
        nnode = node->osdecoder;

        /* First check program name */
        if (lf->program_name) {
            if (nnode->program_name) {
                if (!OSMatch_Execute(lf->program_name, lf->p_name_size,
                                     nnode->program_name)) {
                    continue;
                }
                pmatch = lf->log;
            } else if (nnode->program_name_pcre2) {
                if (!OSPcre2_Execute(lf->program_name, nnode->program_name_pcre2)) {
                    continue;
                }
                pmatch = lf->log;
            }
        }

        /* If prematch fails, go to the next osdecoder in the list */
        if (nnode->prematch) {
            if (!(pmatch = OSRegex_Execute(lf->log, nnode->prematch))) {
                continue;
            }
        }
        else if (nnode->prematch_pcre2) {
            if (!(pmatch = OSPcre2_Execute(lf->log, nnode->prematch_pcre2))) {
                continue;
            }
        }

#ifdef TESTRULE
        if (!alert_only) {
            print_out("       decoder: '%s'", nnode->name);
        }
#endif

        lf->decoder_info = nnode;
        child_node = node->child;

        /* If no child node is set, set the child node
         * as if it were the child (ugh)
         */
        if (!child_node) {
            child_node = node;
        }

        else {
            /* Check if we have any child osdecoder */
            while (child_node) {
                nnode = child_node->osdecoder;

                /* If we have a pre match and it matches, keep
                 * going. If we don't have a prematch, stop
                 * and go for the regexes.
                 */
                if (nnode->prematch) {
                    const char *llog2;

                    /* If we have an offset set, use it */
                    if (nnode->prematch_offset & AFTER_PARENT) {
                        llog2 = pmatch;
                    } else {
                        llog2 = lf->log;
                    }

                    if ((cmatch = OSRegex_Execute(llog2, nnode->prematch))) {
                        lf->decoder_info = nnode;

                        break;
                    }
                } else if (nnode->prematch_pcre2) {
                    const char *llog2;

                    /* If we have an offset set, use it */
                    if (nnode->prematch_offset & AFTER_PARENT) {
                        llog2 = pmatch;
                    } else {
                        llog2 = lf->log;
                    }

                    if ((cmatch = OSPcre2_Execute(llog2, nnode->prematch_pcre2))) {
                        lf->decoder_info = nnode;

                        break;
                    }
                } else {
                    cmatch = pmatch;
                    break;
                }

                /* If we have multiple regex-only childs,
                 * do not attempt to go any further with them.
                 */
                if (child_node->osdecoder->get_next) {
                    do {
                        child_node = child_node->next;
                    } while (child_node && child_node->osdecoder->get_next);

                    if (!child_node) {
                        return;
                    }

                    child_node = child_node->next;
                    nnode = NULL;
                } else {
                    child_node = child_node->next;
                    nnode = NULL;
                }
            }
        }

        /* Nothing matched */
        if (!nnode) {
            return;
        }

        /* If we have an external decoder, execute it */
        if (nnode->plugindecoder) {
            nnode->plugindecoder(lf);
            return;
        }

        /* Get the regex */
        while (child_node) {
            if (nnode->regex) {
                int i = 0;

                /* With regex we have multiple options
                 * regarding the offset:
                 * after the prematch,
                 * after the parent,
                 * after some previous regex,
                 * or any offset
                 */
                if (nnode->regex_offset) {
                    if (nnode->regex_offset & AFTER_PARENT) {
                        llog = pmatch;
                    } else if (nnode->regex_offset & AFTER_PREMATCH) {
                        llog = cmatch;
                    } else if (nnode->regex_offset & AFTER_PREVREGEX) {
                        if (!regex_prev) {
                            llog = cmatch;
                        } else {
                            llog = regex_prev;
                        }
                    }
                } else {
                    llog = lf->log;
                }

                /* If Regex does not match, return */
                if (!(regex_prev = OSRegex_Execute(llog, nnode->regex))) {
                    if (nnode->get_next) {
                        child_node = child_node->next;
                        nnode = child_node->osdecoder;
                        continue;
                    }
                    return;
                }

                while (nnode->regex->sub_strings[i]) {
                    if (nnode->order[i]) {
                        nnode->order[i](lf, nnode->regex->sub_strings[i]);
                        nnode->regex->sub_strings[i] = NULL;
                        i++;
                        continue;
                    }

                    /* We do not free any memory used above */
                    os_free(nnode->regex->sub_strings[i]);
                    nnode->regex->sub_strings[i] = NULL;
                    i++;
                }

                /* If we have a next regex, try getting it */
                if (nnode->get_next) {
                    child_node = child_node->next;
                    nnode = child_node->osdecoder;
                    continue;
                }

                break;
            }
            else if (nnode->pcre2) {
                int i = 0;

                /* With regex we have multiple options
                 * regarding the offset:
                 * after the prematch,
                 * after the parent,
                 * after some previous regex,
                 * or any offset
                 */
                if (nnode->regex_offset) {
                    if (nnode->regex_offset & AFTER_PARENT) {
                        llog = pmatch;
                    } else if (nnode->regex_offset & AFTER_PREMATCH) {
                        llog = cmatch;
                    } else if (nnode->regex_offset & AFTER_PREVREGEX) {
                        if (!regex_prev) {
                            llog = cmatch;
                        } else {
                            llog = regex_prev;
                        }
                    }
                } else {
                    llog = lf->log;
                }

                /* If Regex does not match, return */
                if (!(regex_prev = OSPcre2_Execute(llog, nnode->pcre2))) {
                    if (nnode->get_next) {
                        child_node = child_node->next;
                        nnode = child_node->osdecoder;
                        continue;
                    }
                    return;
                }

                while (nnode->pcre2->sub_strings[i]) {
                    if (nnode->order[i]) {
                        nnode->order[i](lf, nnode->pcre2->sub_strings[i]);
                        nnode->pcre2->sub_strings[i] = NULL;
                        i++;
                        continue;
                    }

                    /* We do not free any memory used above */
                    os_free(nnode->pcre2->sub_strings[i]);
                    nnode->pcre2->sub_strings[i] = NULL;
                    i++;
                }

                /* If we have a next regex, try getting it */
                if (nnode->get_next) {
                    child_node = child_node->next;
                    nnode = child_node->osdecoder;
                    continue;
                }

                break;
            }


            /* If we don't have a regex, we may leave now */
            return;
        }

        /* ok to return  */
        return;
    } while ((node = node->next) != NULL);

#ifdef TESTRULE
    if (!alert_only) {
        print_out("       No decoder matched.");
    }
#endif
}

/*** Event decoders ****/

void *DstUser_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       dstuser: '%s'", field);
    }
#endif

    lf->dstuser = field;
    return (NULL);
}

void *SrcUser_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       srcuser: '%s'", field);
    }
#endif

    lf->srcuser = field;
    return (NULL);
}

void *SrcIP_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       srcip: '%s'", field);
    }
#endif

    lf->srcip = field;

#ifdef LIBGEOIP_ENABLED

    if(!lf->srcgeoip) { 
        lf->srcgeoip = GetGeoInfobyIP(lf->srcip);
    }

    #ifdef TESTRULE
        if (lf->srcgeoip && !alert_only)
            print_out("       srcgeoip: '%s'", lf->srcgeoip);
    #endif

#endif
    return (NULL);

}

void *DstIP_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       dstip: '%s'", field);
    }
#endif

    lf->dstip = field;
#ifdef LIBGEOIP_ENABLED

    if(!lf->dstgeoip) { 
        lf->dstgeoip = GetGeoInfobyIP(lf->dstip);
    }
    #ifdef TESTRULE
        if (lf->dstgeoip && !alert_only)
            print_out("       dstgeoip: '%s'", lf->dstgeoip);
    #endif

#endif
    return (NULL);

}

void *SrcPort_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       srcport: '%s'", field);
    }
#endif

    lf->srcport = field;
    return (NULL);
}

void *DstPort_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       dstport: '%s'", field);
    }
#endif

    lf->dstport = field;
    return (NULL);
}

void *Protocol_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       proto: '%s'", field);
    }
#endif

    lf->protocol = field;
    return (NULL);
}

void *Action_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       action: '%s'", field);
    }
#endif

    lf->action = field;
    return (NULL);
}

void *ID_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       id: '%s'", field);
    }
#endif

    lf->id = field;
    return (NULL);
}

void *Url_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       url: '%s'", field);
    }
#endif

    lf->url = field;
    return (NULL);
}

void *Data_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       extra_data: '%s'", field);
    }
#endif

    lf->data = field;
    return (NULL);
}

void *Status_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       status: '%s'", field);
    }
#endif

    lf->status = field;
    return (NULL);
}

void *SystemName_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       system_name: '%s'", field);
    }
#endif

    lf->systemname = field;
    return (NULL);
}

void *FileName_FP(Eventinfo *lf, char *field)
{
#ifdef TESTRULE
    if (!alert_only) {
        print_out("       filename: '%s'", field);
    }
#endif

    lf->filename = field;
    return (NULL);
}


void *None_FP(__attribute__((unused)) Eventinfo *lf, char *field)
{
    free(field);
    return (NULL);
}

