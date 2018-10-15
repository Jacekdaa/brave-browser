/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/browser/net/brave_httpse_network_delegate_helper.h"

#include "base/task/post_task.h"
#include "base/threading/scoped_blocking_call.h"
#include "brave/browser/brave_browser_process_impl.h"
#include "brave/components/brave_shields/browser/brave_shields_util.h"
#include "brave/components/brave_shields/browser/https_everywhere_service.h"
#include "brave/components/brave_shields/common/brave_shield_constants.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace brave {

void OnBeforeURLRequest_HttpseFileWork(
    GURL* new_url, std::shared_ptr<BraveRequestInfo> ctx) {
  base::ScopedBlockingCall scoped_blocking_call(
      base::BlockingType::WILL_BLOCK);
  DCHECK(ctx->request_identifier != 0);
  g_brave_browser_process->https_everywhere_service()->
    GetHTTPSURL(&ctx->request_url, ctx->request_identifier, ctx->new_url_spec);
}

void OnBeforeURLRequest_HttpsePostFileWork(
    GURL* new_url,
    const ResponseCallback& next_callback,
    std::shared_ptr<BraveRequestInfo> ctx) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  if (!ctx->new_url_spec.empty() &&
    ctx->new_url_spec != ctx->request_url.spec()) {
    *new_url = GURL(ctx->new_url_spec);
    brave_shields::DispatchBlockedEventFromIO(ctx->request_url,
        ctx->render_process_id, ctx->render_frame_id, ctx->frame_tree_node_id,
        brave_shields::kHTTPUpgradableResources);
  }

  next_callback.Run();
}

int OnBeforeURLRequest_HttpsePreFileWork(
    GURL* new_url, const ResponseCallback& next_callback,
    std::shared_ptr<BraveRequestInfo> ctx) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  // Don't try to overwrite an already set URL by another delegate (adblock/tp)
  if (!ctx->new_url_spec.empty()) {
    return net::OK;
  }

  if (ctx->tab_origin.is_empty() || ctx->allow_http_upgradable_resource ||
      !ctx->allow_brave_shields) {
    return net::OK;
  }

  bool is_valid_url = true;
  is_valid_url = ctx->request_url.is_valid();
  std::string scheme = ctx->request_url.scheme();
  if (scheme.length()) {
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), ::tolower);
    if ("http" != scheme && "https" != scheme) {
      is_valid_url = false;
    }
  }

  if (is_valid_url) {
    if (!g_brave_browser_process->https_everywhere_service()->
        GetHTTPSURLFromCacheOnly(&ctx->request_url, ctx->request_identifier,
          ctx->new_url_spec)) {
      g_brave_browser_process->https_everywhere_service()->
        GetTaskRunner()->PostTaskAndReply(FROM_HERE,
          base::Bind(OnBeforeURLRequest_HttpseFileWork, new_url, ctx),
          base::Bind(base::IgnoreResult(
              &OnBeforeURLRequest_HttpsePostFileWork),
              new_url, next_callback, ctx));
      return net::ERR_IO_PENDING;
    } else {
      if (!ctx->new_url_spec.empty()) {
        *new_url = GURL(ctx->new_url_spec);
        brave_shields::DispatchBlockedEventFromIO(ctx->request_url,
            ctx->render_process_id, ctx->render_frame_id,
            ctx->frame_tree_node_id,
            brave_shields::kHTTPUpgradableResources);
      }
    }
  }

  return net::OK;
}

}  // namespace brave
