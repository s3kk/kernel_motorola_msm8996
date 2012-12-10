/*
 * Scanning implementation
 *
 * Copyright 2003, Jouni Malinen <jkmaline@cc.hut.fi>
 * Copyright 2004, Instant802 Networks, Inc.
 * Copyright 2005, Devicescape Software, Inc.
 * Copyright 2006-2007	Jiri Benc <jbenc@suse.cz>
 * Copyright 2007, Michael Wu <flamingice@sourmilk.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/pm_qos.h>
#include <net/sch_generic.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <net/mac80211.h>

#include "ieee80211_i.h"
#include "driver-ops.h"
#include "mesh.h"

#define IEEE80211_PROBE_DELAY (HZ / 33)
#define IEEE80211_CHANNEL_TIME (HZ / 33)
#define IEEE80211_PASSIVE_CHANNEL_TIME (HZ / 8)

static void ieee80211_rx_bss_free(struct cfg80211_bss *cbss)
{
	struct ieee80211_bss *bss = (void *)cbss->priv;

	kfree(bss_mesh_id(bss));
	kfree(bss_mesh_cfg(bss));
}

void ieee80211_rx_bss_put(struct ieee80211_local *local,
			  struct ieee80211_bss *bss)
{
	if (!bss)
		return;
	cfg80211_put_bss(container_of((void *)bss, struct cfg80211_bss, priv));
}

static bool is_uapsd_supported(struct ieee802_11_elems *elems)
{
	u8 qos_info;

	if (elems->wmm_info && elems->wmm_info_len == 7
	    && elems->wmm_info[5] == 1)
		qos_info = elems->wmm_info[6];
	else if (elems->wmm_param && elems->wmm_param_len == 24
		 && elems->wmm_param[5] == 1)
		qos_info = elems->wmm_param[6];
	else
		/* no valid wmm information or parameter element found */
		return false;

	return qos_info & IEEE80211_WMM_IE_AP_QOSINFO_UAPSD;
}

struct ieee80211_bss *
ieee80211_bss_info_update(struct ieee80211_local *local,
			  struct ieee80211_rx_status *rx_status,
			  struct ieee80211_mgmt *mgmt, size_t len,
			  struct ieee802_11_elems *elems,
			  struct ieee80211_channel *channel)
{
	bool beacon = ieee80211_is_beacon(mgmt->frame_control);
	struct cfg80211_bss *cbss;
	struct ieee80211_bss *bss;
	int clen, srlen;
	s32 signal = 0;

	if (local->hw.flags & IEEE80211_HW_SIGNAL_DBM)
		signal = rx_status->signal * 100;
	else if (local->hw.flags & IEEE80211_HW_SIGNAL_UNSPEC)
		signal = (rx_status->signal * 100) / local->hw.max_signal;

	cbss = cfg80211_inform_bss_frame(local->hw.wiphy, channel,
					 mgmt, len, signal, GFP_ATOMIC);
	if (!cbss)
		return NULL;

	cbss->free_priv = ieee80211_rx_bss_free;
	bss = (void *)cbss->priv;

	bss->device_ts = rx_status->device_timestamp;

	if (elems->parse_error) {
		if (beacon)
			bss->corrupt_data |= IEEE80211_BSS_CORRUPT_BEACON;
		else
			bss->corrupt_data |= IEEE80211_BSS_CORRUPT_PROBE_RESP;
	} else {
		if (beacon)
			bss->corrupt_data &= ~IEEE80211_BSS_CORRUPT_BEACON;
		else
			bss->corrupt_data &= ~IEEE80211_BSS_CORRUPT_PROBE_RESP;
	}

	/* save the ERP value so that it is available at association time */
	if (elems->erp_info && elems->erp_info_len >= 1 &&
			(!elems->parse_error ||
			 !(bss->valid_data & IEEE80211_BSS_VALID_ERP))) {
		bss->erp_value = elems->erp_info[0];
		bss->has_erp_value = true;
		if (!elems->parse_error)
			bss->valid_data |= IEEE80211_BSS_VALID_ERP;
	}

	/* replace old supported rates if we get new values */
	if (!elems->parse_error ||
	    !(bss->valid_data & IEEE80211_BSS_VALID_RATES)) {
		srlen = 0;
		if (elems->supp_rates) {
			clen = IEEE80211_MAX_SUPP_RATES;
			if (clen > elems->supp_rates_len)
				clen = elems->supp_rates_len;
			memcpy(bss->supp_rates, elems->supp_rates, clen);
			srlen += clen;
		}
		if (elems->ext_supp_rates) {
			clen = IEEE80211_MAX_SUPP_RATES - srlen;
			if (clen > elems->ext_supp_rates_len)
				clen = elems->ext_supp_rates_len;
			memcpy(bss->supp_rates + srlen, elems->ext_supp_rates,
			       clen);
			srlen += clen;
		}
		if (srlen) {
			bss->supp_rates_len = srlen;
			if (!elems->parse_error)
				bss->valid_data |= IEEE80211_BSS_VALID_RATES;
		}
	}

	if (!elems->parse_error ||
	    !(bss->valid_data & IEEE80211_BSS_VALID_WMM)) {
		bss->wmm_used = elems->wmm_param || elems->wmm_info;
		bss->uapsd_supported = is_uapsd_supported(elems);
		if (!elems->parse_error)
			bss->valid_data |= IEEE80211_BSS_VALID_WMM;
	}

	if (!beacon)
		bss->last_probe_resp = jiffies;

	return bss;
}

void ieee80211_scan_rx(struct ieee80211_local *local, struct sk_buff *skb)
{
	struct ieee80211_rx_status *rx_status = IEEE80211_SKB_RXCB(skb);
	struct ieee80211_sub_if_data *sdata1, *sdata2;
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	struct ieee80211_bss *bss;
	u8 *elements;
	struct ieee80211_channel *channel;
	size_t baselen;
	bool beacon;
	struct ieee802_11_elems elems;

	if (skb->len < 24 ||
	    (!ieee80211_is_probe_resp(mgmt->frame_control) &&
	     !ieee80211_is_beacon(mgmt->frame_control)))
		return;

	sdata1 = rcu_dereference(local->scan_sdata);
	sdata2 = rcu_dereference(local->sched_scan_sdata);

	if (likely(!sdata1 && !sdata2))
		return;

	if (ieee80211_is_probe_resp(mgmt->frame_control)) {
		/* ignore ProbeResp to foreign address */
		if ((!sdata1 || !ether_addr_equal(mgmt->da, sdata1->vif.addr)) &&
		    (!sdata2 || !ether_addr_equal(mgmt->da, sdata2->vif.addr)))
			return;

		elements = mgmt->u.probe_resp.variable;
		baselen = offsetof(struct ieee80211_mgmt, u.probe_resp.variable);
		beacon = false;
	} else {
		baselen = offsetof(struct ieee80211_mgmt, u.beacon.variable);
		elements = mgmt->u.beacon.variable;
		beacon = true;
	}

	if (baselen > skb->len)
		return;

	ieee802_11_parse_elems(elements, skb->len - baselen, &elems);

	channel = ieee80211_get_channel(local->hw.wiphy, rx_status->freq);

	if (!channel || channel->flags & IEEE80211_CHAN_DISABLED)
		return;

	bss = ieee80211_bss_info_update(local, rx_status,
					mgmt, skb->len, &elems,
					channel);
	if (bss)
		ieee80211_rx_bss_put(local, bss);
}

/* return false if no more work */
static bool ieee80211_prep_hw_scan(struct ieee80211_local *local)
{
	struct cfg80211_scan_request *req = local->scan_req;
	enum ieee80211_band band;
	int i, ielen, n_chans;

	do {
		if (local->hw_scan_band == IEEE80211_NUM_BANDS)
			return false;

		band = local->hw_scan_band;
		n_chans = 0;
		for (i = 0; i < req->n_channels; i++) {
			if (req->channels[i]->band == band) {
				local->hw_scan_req->channels[n_chans] =
							req->channels[i];
				n_chans++;
			}
		}

		local->hw_scan_band++;
	} while (!n_chans);

	local->hw_scan_req->n_channels = n_chans;

	ielen = ieee80211_build_preq_ies(local, (u8 *)local->hw_scan_req->ie,
					 local->hw_scan_ies_bufsize,
					 req->ie, req->ie_len, band,
					 req->rates[band], 0);
	local->hw_scan_req->ie_len = ielen;
	local->hw_scan_req->no_cck = req->no_cck;

	return true;
}

static void __ieee80211_scan_completed(struct ieee80211_hw *hw, bool aborted,
				       bool was_hw_scan)
{
	struct ieee80211_local *local = hw_to_local(hw);

	lockdep_assert_held(&local->mtx);

	/*
	 * It's ok to abort a not-yet-running scan (that
	 * we have one at all will be verified by checking
	 * local->scan_req next), but not to complete it
	 * successfully.
	 */
	if (WARN_ON(!local->scanning && !aborted))
		aborted = true;

	if (WARN_ON(!local->scan_req))
		return;

	if (was_hw_scan && !aborted && ieee80211_prep_hw_scan(local)) {
		int rc;

		rc = drv_hw_scan(local,
			rcu_dereference_protected(local->scan_sdata,
						  lockdep_is_held(&local->mtx)),
			local->hw_scan_req);

		if (rc == 0)
			return;
	}

	kfree(local->hw_scan_req);
	local->hw_scan_req = NULL;

	if (local->scan_req != local->int_scan_req)
		cfg80211_scan_done(local->scan_req, aborted);
	local->scan_req = NULL;
	rcu_assign_pointer(local->scan_sdata, NULL);

	local->scanning = 0;
	local->scan_channel = NULL;

	/* Set power back to normal operating levels. */
	ieee80211_hw_config(local, 0);

	if (!was_hw_scan) {
		ieee80211_configure_filter(local);
		drv_sw_scan_complete(local);
		ieee80211_offchannel_return(local, true);
	}

	ieee80211_recalc_idle(local);

	ieee80211_mlme_notify_scan_completed(local);
	ieee80211_ibss_notify_scan_completed(local);
	ieee80211_mesh_notify_scan_completed(local);
	ieee80211_start_next_roc(local);
}

void ieee80211_scan_completed(struct ieee80211_hw *hw, bool aborted)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_scan_completed(local, aborted);

	set_bit(SCAN_COMPLETED, &local->scanning);
	if (aborted)
		set_bit(SCAN_ABORTED, &local->scanning);
	ieee80211_queue_delayed_work(&local->hw, &local->scan_work, 0);
}
EXPORT_SYMBOL(ieee80211_scan_completed);

static int ieee80211_start_sw_scan(struct ieee80211_local *local)
{
	/* Software scan is not supported in multi-channel cases */
	if (local->use_chanctx)
		return -EOPNOTSUPP;

	/*
	 * Hardware/driver doesn't support hw_scan, so use software
	 * scanning instead. First send a nullfunc frame with power save
	 * bit on so that AP will buffer the frames for us while we are not
	 * listening, then send probe requests to each channel and wait for
	 * the responses. After all channels are scanned, tune back to the
	 * original channel and send a nullfunc frame with power save bit
	 * off to trigger the AP to send us all the buffered frames.
	 *
	 * Note that while local->sw_scanning is true everything else but
	 * nullfunc frames and probe requests will be dropped in
	 * ieee80211_tx_h_check_assoc().
	 */
	drv_sw_scan_start(local);

	local->leave_oper_channel_time = jiffies;
	local->next_scan_state = SCAN_DECISION;
	local->scan_channel_idx = 0;

	ieee80211_offchannel_stop_vifs(local, true);

	ieee80211_configure_filter(local);

	/* We need to set power level at maximum rate for scanning. */
	ieee80211_hw_config(local, 0);

	ieee80211_queue_delayed_work(&local->hw,
				     &local->scan_work, 0);

	return 0;
}

static bool ieee80211_can_scan(struct ieee80211_local *local,
			       struct ieee80211_sub_if_data *sdata)
{
	if (!list_empty(&local->roc_list))
		return false;

	if (sdata->vif.type == NL80211_IFTYPE_STATION &&
	    sdata->u.mgd.flags & (IEEE80211_STA_BEACON_POLL |
				  IEEE80211_STA_CONNECTION_POLL))
		return false;

	return true;
}

void ieee80211_run_deferred_scan(struct ieee80211_local *local)
{
	lockdep_assert_held(&local->mtx);

	if (!local->scan_req || local->scanning)
		return;

	if (!ieee80211_can_scan(local,
				rcu_dereference_protected(
					local->scan_sdata,
					lockdep_is_held(&local->mtx))))
		return;

	ieee80211_queue_delayed_work(&local->hw, &local->scan_work,
				     round_jiffies_relative(0));
}

static void ieee80211_scan_state_send_probe(struct ieee80211_local *local,
					    unsigned long *next_delay)
{
	int i;
	struct ieee80211_sub_if_data *sdata;
	enum ieee80211_band band = local->hw.conf.channel->band;

	sdata = rcu_dereference_protected(local->scan_sdata,
					  lockdep_is_held(&local->mtx));

	for (i = 0; i < local->scan_req->n_ssids; i++)
		ieee80211_send_probe_req(
			sdata, NULL,
			local->scan_req->ssids[i].ssid,
			local->scan_req->ssids[i].ssid_len,
			local->scan_req->ie, local->scan_req->ie_len,
			local->scan_req->rates[band], false,
			local->scan_req->no_cck,
			local->hw.conf.channel, true);

	/*
	 * After sending probe requests, wait for probe responses
	 * on the channel.
	 */
	*next_delay = IEEE80211_CHANNEL_TIME;
	local->next_scan_state = SCAN_DECISION;
}

static int __ieee80211_start_scan(struct ieee80211_sub_if_data *sdata,
				  struct cfg80211_scan_request *req)
{
	struct ieee80211_local *local = sdata->local;
	int rc;

	lockdep_assert_held(&local->mtx);

	if (local->scan_req)
		return -EBUSY;

	if (!ieee80211_can_scan(local, sdata)) {
		/* wait for the work to finish/time out */
		local->scan_req = req;
		rcu_assign_pointer(local->scan_sdata, sdata);
		return 0;
	}

	if (local->ops->hw_scan) {
		u8 *ies;

		local->hw_scan_ies_bufsize = 2 + IEEE80211_MAX_SSID_LEN +
					     local->scan_ies_len +
					     req->ie_len;
		local->hw_scan_req = kmalloc(
				sizeof(*local->hw_scan_req) +
				req->n_channels * sizeof(req->channels[0]) +
				local->hw_scan_ies_bufsize, GFP_KERNEL);
		if (!local->hw_scan_req)
			return -ENOMEM;

		local->hw_scan_req->ssids = req->ssids;
		local->hw_scan_req->n_ssids = req->n_ssids;
		ies = (u8 *)local->hw_scan_req +
			sizeof(*local->hw_scan_req) +
			req->n_channels * sizeof(req->channels[0]);
		local->hw_scan_req->ie = ies;
		local->hw_scan_req->flags = req->flags;

		local->hw_scan_band = 0;

		/*
		 * After allocating local->hw_scan_req, we must
		 * go through until ieee80211_prep_hw_scan(), so
		 * anything that might be changed here and leave
		 * this function early must not go after this
		 * allocation.
		 */
	}

	local->scan_req = req;
	rcu_assign_pointer(local->scan_sdata, sdata);

	if (local->ops->hw_scan) {
		__set_bit(SCAN_HW_SCANNING, &local->scanning);
	} else if ((req->n_channels == 1) &&
		   (req->channels[0] == local->_oper_channel)) {
		/*
		 * If we are scanning only on the operating channel
		 * then we do not need to stop normal activities
		 */
		unsigned long next_delay;

		__set_bit(SCAN_ONCHANNEL_SCANNING, &local->scanning);

		ieee80211_recalc_idle(local);

		/* Notify driver scan is starting, keep order of operations
		 * same as normal software scan, in case that matters. */
		drv_sw_scan_start(local);

		ieee80211_configure_filter(local); /* accept probe-responses */

		/* We need to ensure power level is at max for scanning. */
		ieee80211_hw_config(local, 0);

		if ((req->channels[0]->flags &
		     IEEE80211_CHAN_PASSIVE_SCAN) ||
		    !local->scan_req->n_ssids) {
			next_delay = IEEE80211_PASSIVE_CHANNEL_TIME;
		} else {
			ieee80211_scan_state_send_probe(local, &next_delay);
			next_delay = IEEE80211_CHANNEL_TIME;
		}

		/* Now, just wait a bit and we are all done! */
		ieee80211_queue_delayed_work(&local->hw, &local->scan_work,
					     next_delay);
		return 0;
	} else {
		/* Do normal software scan */
		__set_bit(SCAN_SW_SCANNING, &local->scanning);
	}

	ieee80211_recalc_idle(local);

	if (local->ops->hw_scan) {
		WARN_ON(!ieee80211_prep_hw_scan(local));
		rc = drv_hw_scan(local, sdata, local->hw_scan_req);
	} else
		rc = ieee80211_start_sw_scan(local);

	if (rc) {
		kfree(local->hw_scan_req);
		local->hw_scan_req = NULL;
		local->scanning = 0;

		ieee80211_recalc_idle(local);

		local->scan_req = NULL;
		rcu_assign_pointer(local->scan_sdata, NULL);
	}

	return rc;
}

static unsigned long
ieee80211_scan_get_channel_time(struct ieee80211_channel *chan)
{
	/*
	 * TODO: channel switching also consumes quite some time,
	 * add that delay as well to get a better estimation
	 */
	if (chan->flags & IEEE80211_CHAN_PASSIVE_SCAN)
		return IEEE80211_PASSIVE_CHANNEL_TIME;
	return IEEE80211_PROBE_DELAY + IEEE80211_CHANNEL_TIME;
}

static void ieee80211_scan_state_decision(struct ieee80211_local *local,
					  unsigned long *next_delay)
{
	bool associated = false;
	bool tx_empty = true;
	bool bad_latency;
	bool listen_int_exceeded;
	unsigned long min_beacon_int = 0;
	struct ieee80211_sub_if_data *sdata;
	struct ieee80211_channel *next_chan;
	enum mac80211_scan_state next_scan_state;

	/*
	 * check if at least one STA interface is associated,
	 * check if at least one STA interface has pending tx frames
	 * and grab the lowest used beacon interval
	 */
	mutex_lock(&local->iflist_mtx);
	list_for_each_entry(sdata, &local->interfaces, list) {
		if (!ieee80211_sdata_running(sdata))
			continue;

		if (sdata->vif.type == NL80211_IFTYPE_STATION) {
			if (sdata->u.mgd.associated) {
				associated = true;

				if (sdata->vif.bss_conf.beacon_int <
				    min_beacon_int || min_beacon_int == 0)
					min_beacon_int =
						sdata->vif.bss_conf.beacon_int;

				if (!qdisc_all_tx_empty(sdata->dev)) {
					tx_empty = false;
					break;
				}
			}
		}
	}
	mutex_unlock(&local->iflist_mtx);

	next_chan = local->scan_req->channels[local->scan_channel_idx];

	/*
	 * we're currently scanning a different channel, let's
	 * see if we can scan another channel without interfering
	 * with the current traffic situation.
	 *
	 * Since we don't know if the AP has pending frames for us
	 * we can only check for our tx queues and use the current
	 * pm_qos requirements for rx. Hence, if no tx traffic occurs
	 * at all we will scan as many channels in a row as the pm_qos
	 * latency allows us to. Additionally we also check for the
	 * currently negotiated listen interval to prevent losing
	 * frames unnecessarily.
	 *
	 * Otherwise switch back to the operating channel.
	 */

	bad_latency = time_after(jiffies +
			ieee80211_scan_get_channel_time(next_chan),
			local->leave_oper_channel_time +
			usecs_to_jiffies(pm_qos_request(PM_QOS_NETWORK_LATENCY)));

	listen_int_exceeded = time_after(jiffies +
			ieee80211_scan_get_channel_time(next_chan),
			local->leave_oper_channel_time +
			usecs_to_jiffies(min_beacon_int * 1024) *
			local->hw.conf.listen_interval);

	if (associated && !tx_empty) {
		if (local->scan_req->flags & NL80211_SCAN_FLAG_LOW_PRIORITY)
			next_scan_state = SCAN_ABORT;
		else
			next_scan_state = SCAN_SUSPEND;
	} else if (associated && (bad_latency || listen_int_exceeded)) {
		next_scan_state = SCAN_SUSPEND;
	} else {
		next_scan_state = SCAN_SET_CHANNEL;
	}

	local->next_scan_state = next_scan_state;

	*next_delay = 0;
}

static void ieee80211_scan_state_set_channel(struct ieee80211_local *local,
					     unsigned long *next_delay)
{
	int skip;
	struct ieee80211_channel *chan;

	skip = 0;
	chan = local->scan_req->channels[local->scan_channel_idx];

	local->scan_channel = chan;

	if (ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL))
		skip = 1;

	/* advance state machine to next channel/band */
	local->scan_channel_idx++;

	if (skip) {
		/* if we skip this channel return to the decision state */
		local->next_scan_state = SCAN_DECISION;
		return;
	}

	/*
	 * Probe delay is used to update the NAV, cf. 11.1.3.2.2
	 * (which unfortunately doesn't say _why_ step a) is done,
	 * but it waits for the probe delay or until a frame is
	 * received - and the received frame would update the NAV).
	 * For now, we do not support waiting until a frame is
	 * received.
	 *
	 * In any case, it is not necessary for a passive scan.
	 */
	if (chan->flags & IEEE80211_CHAN_PASSIVE_SCAN ||
	    !local->scan_req->n_ssids) {
		*next_delay = IEEE80211_PASSIVE_CHANNEL_TIME;
		local->next_scan_state = SCAN_DECISION;
		return;
	}

	/* active scan, send probes */
	*next_delay = IEEE80211_PROBE_DELAY;
	local->next_scan_state = SCAN_SEND_PROBE;
}

static void ieee80211_scan_state_suspend(struct ieee80211_local *local,
					 unsigned long *next_delay)
{
	/* switch back to the operating channel */
	local->scan_channel = NULL;
	ieee80211_hw_config(local, IEEE80211_CONF_CHANGE_CHANNEL);

	/*
	 * Re-enable vifs and beaconing.  Leave PS
	 * in off-channel state..will put that back
	 * on-channel at the end of scanning.
	 */
	ieee80211_offchannel_return(local, false);

	*next_delay = HZ / 5;
	/* afterwards, resume scan & go to next channel */
	local->next_scan_state = SCAN_RESUME;
}

static void ieee80211_scan_state_resume(struct ieee80211_local *local,
					unsigned long *next_delay)
{
	/* PS already is in off-channel mode */
	ieee80211_offchannel_stop_vifs(local, false);

	if (local->ops->flush) {
		drv_flush(local, false);
		*next_delay = 0;
	} else
		*next_delay = HZ / 10;

	/* remember when we left the operating channel */
	local->leave_oper_channel_time = jiffies;

	/* advance to the next channel to be scanned */
	local->next_scan_state = SCAN_SET_CHANNEL;
}

void ieee80211_scan_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local, scan_work.work);
	struct ieee80211_sub_if_data *sdata;
	unsigned long next_delay = 0;
	bool aborted, hw_scan;

	mutex_lock(&local->mtx);

	sdata = rcu_dereference_protected(local->scan_sdata,
					  lockdep_is_held(&local->mtx));

	/* When scanning on-channel, the first-callback means completed. */
	if (test_bit(SCAN_ONCHANNEL_SCANNING, &local->scanning)) {
		aborted = test_and_clear_bit(SCAN_ABORTED, &local->scanning);
		goto out_complete;
	}

	if (test_and_clear_bit(SCAN_COMPLETED, &local->scanning)) {
		aborted = test_and_clear_bit(SCAN_ABORTED, &local->scanning);
		goto out_complete;
	}

	if (!sdata || !local->scan_req)
		goto out;

	if (local->scan_req && !local->scanning) {
		struct cfg80211_scan_request *req = local->scan_req;
		int rc;

		local->scan_req = NULL;
		rcu_assign_pointer(local->scan_sdata, NULL);

		rc = __ieee80211_start_scan(sdata, req);
		if (rc) {
			/* need to complete scan in cfg80211 */
			local->scan_req = req;
			aborted = true;
			goto out_complete;
		} else
			goto out;
	}

	/*
	 * Avoid re-scheduling when the sdata is going away.
	 */
	if (!ieee80211_sdata_running(sdata)) {
		aborted = true;
		goto out_complete;
	}

	/*
	 * as long as no delay is required advance immediately
	 * without scheduling a new work
	 */
	do {
		if (!ieee80211_sdata_running(sdata)) {
			aborted = true;
			goto out_complete;
		}

		switch (local->next_scan_state) {
		case SCAN_DECISION:
			/* if no more bands/channels left, complete scan */
			if (local->scan_channel_idx >= local->scan_req->n_channels) {
				aborted = false;
				goto out_complete;
			}
			ieee80211_scan_state_decision(local, &next_delay);
			break;
		case SCAN_SET_CHANNEL:
			ieee80211_scan_state_set_channel(local, &next_delay);
			break;
		case SCAN_SEND_PROBE:
			ieee80211_scan_state_send_probe(local, &next_delay);
			break;
		case SCAN_SUSPEND:
			ieee80211_scan_state_suspend(local, &next_delay);
			break;
		case SCAN_RESUME:
			ieee80211_scan_state_resume(local, &next_delay);
			break;
		case SCAN_ABORT:
			aborted = true;
			goto out_complete;
		}
	} while (next_delay == 0);

	ieee80211_queue_delayed_work(&local->hw, &local->scan_work, next_delay);
	goto out;

out_complete:
	hw_scan = test_bit(SCAN_HW_SCANNING, &local->scanning);
	__ieee80211_scan_completed(&local->hw, aborted, hw_scan);
out:
	mutex_unlock(&local->mtx);
}

int ieee80211_request_scan(struct ieee80211_sub_if_data *sdata,
			   struct cfg80211_scan_request *req)
{
	int res;

	mutex_lock(&sdata->local->mtx);
	res = __ieee80211_start_scan(sdata, req);
	mutex_unlock(&sdata->local->mtx);

	return res;
}

int ieee80211_request_ibss_scan(struct ieee80211_sub_if_data *sdata,
				const u8 *ssid, u8 ssid_len,
				struct ieee80211_channel *chan)
{
	struct ieee80211_local *local = sdata->local;
	int ret = -EBUSY;
	enum ieee80211_band band;

	mutex_lock(&local->mtx);

	/* busy scanning */
	if (local->scan_req)
		goto unlock;

	/* fill internal scan request */
	if (!chan) {
		int i, max_n;
		int n_ch = 0;

		for (band = 0; band < IEEE80211_NUM_BANDS; band++) {
			if (!local->hw.wiphy->bands[band])
				continue;

			max_n = local->hw.wiphy->bands[band]->n_channels;
			for (i = 0; i < max_n; i++) {
				struct ieee80211_channel *tmp_ch =
				    &local->hw.wiphy->bands[band]->channels[i];

				if (tmp_ch->flags & (IEEE80211_CHAN_NO_IBSS |
						     IEEE80211_CHAN_DISABLED))
					continue;

				local->int_scan_req->channels[n_ch] = tmp_ch;
				n_ch++;
			}
		}

		if (WARN_ON_ONCE(n_ch == 0))
			goto unlock;

		local->int_scan_req->n_channels = n_ch;
	} else {
		if (WARN_ON_ONCE(chan->flags & (IEEE80211_CHAN_NO_IBSS |
						IEEE80211_CHAN_DISABLED)))
			goto unlock;

		local->int_scan_req->channels[0] = chan;
		local->int_scan_req->n_channels = 1;
	}

	local->int_scan_req->ssids = &local->scan_ssid;
	local->int_scan_req->n_ssids = 1;
	memcpy(local->int_scan_req->ssids[0].ssid, ssid, IEEE80211_MAX_SSID_LEN);
	local->int_scan_req->ssids[0].ssid_len = ssid_len;

	ret = __ieee80211_start_scan(sdata, sdata->local->int_scan_req);
 unlock:
	mutex_unlock(&local->mtx);
	return ret;
}

/*
 * Only call this function when a scan can't be queued -- under RTNL.
 */
void ieee80211_scan_cancel(struct ieee80211_local *local)
{
	/*
	 * We are canceling software scan, or deferred scan that was not
	 * yet really started (see __ieee80211_start_scan ).
	 *
	 * Regarding hardware scan:
	 * - we can not call  __ieee80211_scan_completed() as when
	 *   SCAN_HW_SCANNING bit is set this function change
	 *   local->hw_scan_req to operate on 5G band, what race with
	 *   driver which can use local->hw_scan_req
	 *
	 * - we can not cancel scan_work since driver can schedule it
	 *   by ieee80211_scan_completed(..., true) to finish scan
	 *
	 * Hence we only call the cancel_hw_scan() callback, but the low-level
	 * driver is still responsible for calling ieee80211_scan_completed()
	 * after the scan was completed/aborted.
	 */

	mutex_lock(&local->mtx);
	if (!local->scan_req)
		goto out;

	if (test_bit(SCAN_HW_SCANNING, &local->scanning)) {
		if (local->ops->cancel_hw_scan)
			drv_cancel_hw_scan(local,
				rcu_dereference_protected(local->scan_sdata,
						lockdep_is_held(&local->mtx)));
		goto out;
	}

	/*
	 * If the work is currently running, it must be blocked on
	 * the mutex, but we'll set scan_sdata = NULL and it'll
	 * simply exit once it acquires the mutex.
	 */
	cancel_delayed_work(&local->scan_work);
	/* and clean up */
	__ieee80211_scan_completed(&local->hw, true, false);
out:
	mutex_unlock(&local->mtx);
}

int ieee80211_request_sched_scan_start(struct ieee80211_sub_if_data *sdata,
				       struct cfg80211_sched_scan_request *req)
{
	struct ieee80211_local *local = sdata->local;
	struct ieee80211_sched_scan_ies sched_scan_ies = {};
	int ret, i, iebufsz;

	iebufsz = 2 + IEEE80211_MAX_SSID_LEN +
		  local->scan_ies_len + req->ie_len;

	mutex_lock(&local->mtx);

	if (rcu_access_pointer(local->sched_scan_sdata)) {
		ret = -EBUSY;
		goto out;
	}

	if (!local->ops->sched_scan_start) {
		ret = -ENOTSUPP;
		goto out;
	}

	for (i = 0; i < IEEE80211_NUM_BANDS; i++) {
		if (!local->hw.wiphy->bands[i])
			continue;

		sched_scan_ies.ie[i] = kzalloc(iebufsz, GFP_KERNEL);
		if (!sched_scan_ies.ie[i]) {
			ret = -ENOMEM;
			goto out_free;
		}

		sched_scan_ies.len[i] =
			ieee80211_build_preq_ies(local, sched_scan_ies.ie[i],
						 iebufsz, req->ie, req->ie_len,
						 i, (u32) -1, 0);
	}

	ret = drv_sched_scan_start(local, sdata, req, &sched_scan_ies);
	if (ret == 0)
		rcu_assign_pointer(local->sched_scan_sdata, sdata);

out_free:
	while (i > 0)
		kfree(sched_scan_ies.ie[--i]);
out:
	mutex_unlock(&local->mtx);
	return ret;
}

int ieee80211_request_sched_scan_stop(struct ieee80211_sub_if_data *sdata)
{
	struct ieee80211_local *local = sdata->local;
	int ret = 0;

	mutex_lock(&local->mtx);

	if (!local->ops->sched_scan_stop) {
		ret = -ENOTSUPP;
		goto out;
	}

	if (rcu_access_pointer(local->sched_scan_sdata))
		drv_sched_scan_stop(local, sdata);

out:
	mutex_unlock(&local->mtx);

	return ret;
}

void ieee80211_sched_scan_results(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_sched_scan_results(local);

	cfg80211_sched_scan_results(hw->wiphy);
}
EXPORT_SYMBOL(ieee80211_sched_scan_results);

void ieee80211_sched_scan_stopped_work(struct work_struct *work)
{
	struct ieee80211_local *local =
		container_of(work, struct ieee80211_local,
			     sched_scan_stopped_work);

	mutex_lock(&local->mtx);

	if (!rcu_access_pointer(local->sched_scan_sdata)) {
		mutex_unlock(&local->mtx);
		return;
	}

	rcu_assign_pointer(local->sched_scan_sdata, NULL);

	mutex_unlock(&local->mtx);

	cfg80211_sched_scan_stopped(local->hw.wiphy);
}

void ieee80211_sched_scan_stopped(struct ieee80211_hw *hw)
{
	struct ieee80211_local *local = hw_to_local(hw);

	trace_api_sched_scan_stopped(local);

	ieee80211_queue_work(&local->hw, &local->sched_scan_stopped_work);
}
EXPORT_SYMBOL(ieee80211_sched_scan_stopped);
