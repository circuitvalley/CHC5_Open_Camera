// SPDX-License-Identifier: GPL-2.0-only
/*
 * CHC5 GigE Vision stream transmitter
 *
 * Copyright (c) 2026 Circuit Valley - Author: Gaurav Singh
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/dma-mapping.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/atomic.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/skbuff.h>
#include <linux/in.h>
#include <linux/rcupdate.h>
#include <linux/net.h>
#include <linux/socket.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/page_ref.h>
#include <linux/sched/rt.h>
#include <linux/macb_zynq_ptp.h>
#include <uapi/linux/sched/types.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/checksum.h>
#include <net/route.h>

#include "gvsp_tx.h"

#define DRIVER_NAME   "vdma_gigev"
#define GVSP_MAX_DEVS 4

#define N_BUF  5

#define GVSP_MAX_DIM            16384u
#define GVSP_MAX_FRAME_BYTES    (256u * 1024u * 1024u)

enum buf_state {
	BUF_FREE        = 0,
	BUF_READY       = 1,
	BUF_ETH_READING = 2,
};

struct gvsp_buf {
	void           *vaddr;
	dma_addr_t      paddr;
	size_t          size;
	enum buf_state  state;
	u64             seq;
	u64             ts_ns;
};

#define N_TX_SKB  2048

#define XMIT_BATCH_SIZE  64

#define GVSP_HDR_ROOM  (NET_SKB_PAD + ETH_HLEN        \
                        + sizeof(struct iphdr)          \
                        + sizeof(struct udphdr)         \
                        + sizeof(struct gvsp_hdr))

#define GVSP_LINEAR_MAX  (NET_SKB_PAD + ETH_HLEN             \
                          + sizeof(struct iphdr)              \
                          + sizeof(struct udphdr)             \
                          + sizeof(struct gvsp_leader))

struct __packed gvsp_hdr {
	__be16  status;
	__be16  block_id;
	__u8    pkt_fmt;
	__u8    pkt_id_hi;
	__be16  pkt_id;
};

#define GVSP_PKT_ID_MAX  0xFFFFFFu

struct __packed gvsp_leader {
	struct gvsp_hdr hdr;
	__be16  _reserved;
	__be16  payload_type;
	__be64  timestamp_ns;
	__be32  pixel_format;
	__be32  size_x;
	__be32  size_y;
	__be32  offset_x;
	__be32  offset_y;
	__be16  padding_x;
	__be16  padding_y;
};

struct __packed gvsp_trailer {
	struct gvsp_hdr hdr;
	__be32  payload_type;
	__be64  data_size;
};

struct gvsp_dev {
	struct platform_device  *pdev;
	struct cdev              cdev;
	struct device           *chr_dev;
	int                      minor;

	struct gvsp_buf   bufs[N_BUF];
	spinlock_t        buf_lock;
	int               vdma_cur_wr;
	int               eth_read_idx;
	int               ready_idx;
	u64               vdma_seq;

	void       *ring_vaddr;
	dma_addr_t  ring_paddr;
	size_t      ring_size;

	void __iomem   *vdma_regs;
	int             vdma_irq;

	struct gvsp_stream_cfg  cfg;
	bool                    streaming;
	spinlock_t              cfg_lock;

	struct mutex            ctl_lock;

	struct task_struct  *tx_thread;
	struct completion    frame_ready;

	struct net_device  *ndev;
	__be32              src_ip;
	__be32              dst_ip;
	u16                 src_port;
	u16                 dst_port;
	unsigned char       dst_mac[ETH_ALEN];
	unsigned char       src_mac[ETH_ALEN];

	struct sk_buff  *tx_skbs[N_TX_SKB];
	int              tx_skb_head;

	struct gvsp_stats  stats;
	spinlock_t         stats_lock;

	u64                ts_zero_ns;
	spinlock_t         ts_lock;

	u16  block_id;

	#define FRAME_LOG_SIZE 256
	struct {
		int   buf_idx;
		u64   seq;
		u32   first4;
	} frame_log[FRAME_LOG_SIZE];
	u32 frame_log_pos;
};

static dev_t             gvsp_devt;
static struct class     *gvsp_class;
static struct gvsp_dev  *gvsp_devs[GVSP_MAX_DEVS];
static DEFINE_MUTEX(gvsp_devs_lock);

static int gvsp_alloc_bufs(struct gvsp_dev *gd, size_t frame_sz)
{
	size_t     total    = frame_sz * N_BUF;
	void      *vbase;
	dma_addr_t pbase;
	int        i;

	if (WARN_ON(gd->ring_vaddr))
		return -EBUSY;

	vbase = dma_alloc_coherent(&gd->pdev->dev, total, &pbase, GFP_KERNEL);
	if (!vbase) {
		dev_err(&gd->pdev->dev,
		        "dma_alloc_coherent: %zu B failed\n", total);
		return -ENOMEM;
	}

	gd->ring_vaddr = vbase;
	gd->ring_paddr = pbase;
	gd->ring_size  = total;

	for (i = 0; i < N_BUF; i++) {
		gd->bufs[i].vaddr   = (u8 *)vbase + (size_t)i * frame_sz;
		gd->bufs[i].paddr   = pbase + (dma_addr_t)i * frame_sz;
		gd->bufs[i].size    = frame_sz;
		gd->bufs[i].state   = BUF_FREE;
		gd->bufs[i].seq     = 0;
	}

	return 0;
}

static void gvsp_free_bufs(struct gvsp_dev *gd)
{
	if (gd->ring_vaddr) {
		dma_free_coherent(&gd->pdev->dev, gd->ring_size,
		                  gd->ring_vaddr, gd->ring_paddr);
		gd->ring_vaddr = NULL;
	}
}

static struct sk_buff *gvsp_alloc_tx_skb(struct gvsp_dev *gd)
{
	struct sk_buff *skb;

	skb = alloc_skb(GVSP_LINEAR_MAX, GFP_KERNEL);
	if (!skb)
		return NULL;

	skb_reserve(skb, NET_SKB_PAD + ETH_HLEN +
	                 sizeof(struct iphdr) + sizeof(struct udphdr));

	skb_put(skb, sizeof(struct gvsp_hdr));
	memset(skb->data, 0, sizeof(struct gvsp_hdr));

	{
		struct udphdr *udph =
			(struct udphdr *)skb_push(skb, sizeof(*udph));
		udph->source = htons(gd->src_port);
		udph->dest   = htons(gd->dst_port);
		udph->len    = 0;
		udph->check  = 0;
	}

	{
		struct iphdr *iph =
			(struct iphdr *)skb_push(skb, sizeof(*iph));
		iph->version  = 4;
		iph->ihl      = 5;
		iph->tos      = IPTOS_LOWDELAY;
		iph->tot_len  = 0;
		iph->id       = 0;
		iph->frag_off = htons(IP_DF);
		iph->ttl      = 64;
		iph->protocol = IPPROTO_UDP;
		iph->check    = 0;
		iph->saddr    = gd->src_ip;
		iph->daddr    = gd->dst_ip;
	}

	{
		struct ethhdr *eth =
			(struct ethhdr *)skb_push(skb, ETH_HLEN);
		memcpy(eth->h_dest,   gd->dst_mac, ETH_ALEN);
		memcpy(eth->h_source, gd->src_mac, ETH_ALEN);
		eth->h_proto = htons(ETH_P_IP);
	}

	skb->dev      = gd->ndev;
	skb->protocol = htons(ETH_P_IP);
	skb->priority = TC_PRIO_INTERACTIVE;
	skb_reset_mac_header(skb);
	skb_set_network_header(skb, ETH_HLEN);
	skb_set_transport_header(skb, ETH_HLEN + sizeof(struct iphdr));

	skb->ip_summed  = CHECKSUM_PARTIAL;
	skb->csum_start = skb_transport_header(skb) - skb->head;
	skb->csum_offset = offsetof(struct udphdr, check);

	return skb;
}

static int gvsp_alloc_tx_ring(struct gvsp_dev *gd)
{
	int i;

	gd->tx_skb_head = 0;

	for (i = 0; i < N_TX_SKB; i++) {
		gd->tx_skbs[i] = gvsp_alloc_tx_skb(gd);
		if (!gd->tx_skbs[i]) {
			dev_err(&gd->pdev->dev,
			        "alloc tx_skbs[%d] failed\n", i);
			while (--i >= 0) {
				kfree_skb(gd->tx_skbs[i]);
				gd->tx_skbs[i] = NULL;
			}
			return -ENOMEM;
		}
	}
	return 0;
}

static void gvsp_free_tx_ring(struct gvsp_dev *gd)
{
	int i, wait_ms = 0;

	for (;;) {
		bool all_free = true;
		for (i = 0; i < N_TX_SKB; i++) {
			if (gd->tx_skbs[i] && skb_shared(gd->tx_skbs[i]))
				all_free = false;
		}
		if (all_free) break;
		if (wait_ms >= 1000) {
			dev_warn(&gd->pdev->dev, "TX SKB ring drain timeout\n");
			break;
		}
		msleep(10);
		wait_ms += 10;
	}

	for (i = 0; i < N_TX_SKB; i++) {
		if (gd->tx_skbs[i]) {
			kfree_skb(gd->tx_skbs[i]);
			gd->tx_skbs[i] = NULL;
		}
	}
}

static struct sk_buff *gvsp_txring_get(struct gvsp_dev *gd)
{
	int retries = 0;

	for (;;) {
		struct sk_buff *skb = gd->tx_skbs[gd->tx_skb_head];

		if (!skb_shared(skb)) {
			gd->tx_skb_head = (gd->tx_skb_head + 1) % N_TX_SKB;
			return skb;
		}

		if (kthread_should_stop())
			return NULL;

		if (++retries > 50000) {
			dev_warn_ratelimited(&gd->pdev->dev,
			    "TX SKB ring stalled\n");
			return NULL;
		}

		if (retries < 200)
			cpu_relax();
		else
			usleep_range(1, 5);
	}
}

#define VDMA_S2MM_DMACR          0x30
#define VDMA_S2MM_DMASR          0x34
#define VDMA_S2MM_FRMSTORE       0x18
#define VDMA_S2MM_FRMPTR_STS     0x24

#define VDMA_S2MM_VSIZE          0xA0
#define VDMA_S2MM_HSIZE          0xA4
#define VDMA_S2MM_FRMDLY_STRIDE  0xA8
#define VDMA_S2MM_START_ADDR(n)  (0xAC + 4 * (n))

#define VDMA_DMACR_RS            BIT(0)
#define VDMA_DMACR_CIRC          BIT(1)
#define VDMA_DMACR_RESET         BIT(2)
#define VDMA_DMACR_FRM_CNT_IRQ   BIT(12)
#define VDMA_DMACR_ERR_IRQ       BIT(14)
#define VDMA_DMACR_FRMCNT_SHIFT  16

#define VDMA_DMASR_HALTED        BIT(0)
#define VDMA_DMASR_FRM_CNT_IRQ   BIT(12)
#define VDMA_DMASR_ERR_IRQ       BIT(14)
#define VDMA_DMASR_ALL_IRQ       (VDMA_DMASR_FRM_CNT_IRQ | \
                                   VDMA_DMASR_ERR_IRQ)
#define VDMA_DMASR_ALL_ERR       0x00007FF0

#define VDMA_FRMPTR_S2MM_SHIFT   16
#define VDMA_FRMPTR_S2MM_MASK    GENMASK(20, 16)

static inline u32 vdma_rd(struct gvsp_dev *gd, u32 reg)
{
	return ioread32(gd->vdma_regs + reg);
}

static inline void vdma_wr(struct gvsp_dev *gd, u32 reg, u32 val)
{
	iowrite32(val, gd->vdma_regs + reg);
}

static irqreturn_t gvsp_vdma_irq(int irq, void *arg)
{
	struct gvsp_dev *gd = arg;
	u32 status;
	int cur_wr, just_done;
	unsigned long flags;
	u64 now_ns;

	status = vdma_rd(gd, VDMA_S2MM_DMASR);
	if (!(status & VDMA_DMASR_ALL_IRQ))
		return IRQ_NONE;

	if (gd->cfg.ts_clock == GVSP_TS_CLOCK_PHC) {
		if (!gd->ndev || macb_zynq_ptp_now_ns(gd->ndev, &now_ns)) {
			now_ns = 0;
			dev_warn_ratelimited(&gd->pdev->dev,
				"PTP clock unavailable, frame sent without timestamp\n");
		}
	} else {
		u64 zero;

		now_ns = ktime_get_ns();
		spin_lock(&gd->ts_lock);
		zero = gd->ts_zero_ns;
		spin_unlock(&gd->ts_lock);
		now_ns = now_ns > zero ? now_ns - zero : 0;
	}

	vdma_wr(gd, VDMA_S2MM_DMASR, status & VDMA_DMASR_ALL_IRQ);

	if (status & VDMA_DMASR_ERR_IRQ) {
		dev_err_ratelimited(&gd->pdev->dev,
			"VDMA S2MM error: SR=0x%08x\n", status);
		vdma_wr(gd, VDMA_S2MM_DMASR, status & VDMA_DMASR_ALL_ERR);
	}

	
	if (!(status & VDMA_DMASR_FRM_CNT_IRQ))
		return IRQ_HANDLED;

	
	just_done = gd->vdma_cur_wr;
	gd->vdma_cur_wr = (just_done + 1) % N_BUF;

	spin_lock_irqsave(&gd->buf_lock, flags);

	if (gd->bufs[just_done].state == BUF_ETH_READING) {
		spin_lock(&gd->stats_lock);
		gd->stats.frames_dropped++;
		spin_unlock(&gd->stats_lock);
		spin_unlock_irqrestore(&gd->buf_lock, flags);
		return IRQ_HANDLED;
	}

	gd->bufs[just_done].state = BUF_READY;
	gd->bufs[just_done].seq   = ++gd->vdma_seq;
	gd->bufs[just_done].ts_ns = now_ns;
	gd->ready_idx             = just_done;

	spin_unlock_irqrestore(&gd->buf_lock, flags);

	{
		u32 pos = gd->frame_log_pos % FRAME_LOG_SIZE;
		gd->frame_log[pos].buf_idx = just_done;
		gd->frame_log[pos].seq     = gd->bufs[just_done].seq;
		gd->frame_log[pos].first4  = *(u32 *)gd->bufs[just_done].vaddr;
		gd->frame_log_pos++;
	}

	complete(&gd->frame_ready);
	return IRQ_HANDLED;
}

static int gvsp_vdma_reset(struct gvsp_dev *gd)
{
	u32 val;
	int timeout = 10000;

	vdma_wr(gd, VDMA_S2MM_DMACR,
	         vdma_rd(gd, VDMA_S2MM_DMACR) | VDMA_DMACR_RESET);

	while (timeout--) {
		val = vdma_rd(gd, VDMA_S2MM_DMACR);
		if (!(val & VDMA_DMACR_RESET))
			return 0;
		udelay(1);
	}

	dev_err(&gd->pdev->dev, "VDMA S2MM reset timeout\n");
	return -ETIMEDOUT;
}

static int gvsp_start_vdma(struct gvsp_dev *gd)
{
	struct device_node *vdma_np;
	u32 stride, dmacr, val;
	int ret, i, timeout;

	vdma_np = of_parse_phandle(gd->pdev->dev.of_node, "vdma-node", 0);
	if (!vdma_np) {
		dev_err(&gd->pdev->dev, "missing 'vdma-node' phandle in DT\n");
		return -ENODEV;
	}

	gd->vdma_regs = of_iomap(vdma_np, 0);
	if (!gd->vdma_regs) {
		dev_err(&gd->pdev->dev, "cannot iomap VDMA registers\n");
		of_node_put(vdma_np);
		return -ENOMEM;
	}

	gd->vdma_irq = of_irq_get(vdma_np, 1);
	if (gd->vdma_irq < 0)
		gd->vdma_irq = of_irq_get(vdma_np, 0);
	of_node_put(vdma_np);

	if (gd->vdma_irq < 0) {
		dev_err(&gd->pdev->dev, "cannot get VDMA S2MM IRQ\n");
		iounmap(gd->vdma_regs);
		gd->vdma_regs = NULL;
		return -ENODEV;
	}

	ret = gvsp_vdma_reset(gd);
	if (ret) {
		iounmap(gd->vdma_regs);
		gd->vdma_regs = NULL;
		return ret;
	}

	timeout = 10000;
	while (timeout--) {
		if (vdma_rd(gd, VDMA_S2MM_DMASR) & VDMA_DMASR_HALTED)
			break;
		udelay(1);
	}

	vdma_wr(gd, VDMA_S2MM_FRMSTORE, N_BUF);

	dmacr = VDMA_DMACR_CIRC
	      | VDMA_DMACR_FRM_CNT_IRQ
	      | VDMA_DMACR_ERR_IRQ
	      | (1 << VDMA_DMACR_FRMCNT_SHIFT);
	vdma_wr(gd, VDMA_S2MM_DMACR, dmacr);

	dmacr |= VDMA_DMACR_RS;
	vdma_wr(gd, VDMA_S2MM_DMACR, dmacr);

	timeout = 10000;
	while (timeout--) {
		val = vdma_rd(gd, VDMA_S2MM_DMASR);
		if (!(val & VDMA_DMASR_HALTED))
			break;
		udelay(1);
	}
	if (val & VDMA_DMASR_HALTED) {
		dev_err(&gd->pdev->dev, "VDMA S2MM failed to start\n");
		iounmap(gd->vdma_regs);
		gd->vdma_regs = NULL;
		return -EIO;
	}

	for (i = 0; i < N_BUF; i++)
		vdma_wr(gd, VDMA_S2MM_START_ADDR(i), (u32)gd->bufs[i].paddr);

	stride = (u32)(gd->bufs[0].size / gd->cfg.height);

	vdma_wr(gd, VDMA_S2MM_FRMDLY_STRIDE, stride);
	vdma_wr(gd, VDMA_S2MM_HSIZE, stride);

	vdma_wr(gd, VDMA_S2MM_VSIZE, gd->cfg.height);

	ret = request_irq(gd->vdma_irq, gvsp_vdma_irq, IRQF_SHARED,
	                  "gvsp-vdma-s2mm", gd);
	if (ret) {
		dev_err(&gd->pdev->dev,
		        "request_irq(%d) failed: %d\n", gd->vdma_irq, ret);
		vdma_wr(gd, VDMA_S2MM_DMACR,
		         vdma_rd(gd, VDMA_S2MM_DMACR) & ~VDMA_DMACR_RS);
		iounmap(gd->vdma_regs);
		gd->vdma_regs = NULL;
		return ret;
	}

	gd->vdma_cur_wr = 0;

	return 0;
}

static void gvsp_stop_vdma(struct gvsp_dev *gd)
{
	if (gd->vdma_regs) {
		int timeout = 10000;

		vdma_wr(gd, VDMA_S2MM_DMACR,
		         vdma_rd(gd, VDMA_S2MM_DMACR) & ~VDMA_DMACR_RS);

		while (timeout--) {
			if (vdma_rd(gd, VDMA_S2MM_DMASR) & VDMA_DMASR_HALTED)
				break;
			udelay(1);
		}

		vdma_wr(gd, VDMA_S2MM_DMACR,
		         vdma_rd(gd, VDMA_S2MM_DMACR) &
		         ~(VDMA_DMACR_FRM_CNT_IRQ | VDMA_DMACR_ERR_IRQ));

		free_irq(gd->vdma_irq, gd);
		gvsp_vdma_reset(gd);
		iounmap(gd->vdma_regs);
		gd->vdma_regs = NULL;
	}
}

static int gvsp_resolve_dst_mac(struct gvsp_dev *gd)
{
	struct neighbour *neigh;
	struct rtable    *rt;
	struct flowi4     fl4 = {
		.daddr = gd->dst_ip,
		.saddr = gd->src_ip,
	};
	int i;

	rt = ip_route_output_key(&init_net, &fl4);
	if (IS_ERR(rt)) {
		dev_err(&gd->pdev->dev,
		        "ip_route_output: %ld\n", PTR_ERR(rt));
		return PTR_ERR(rt);
	}
	ip_rt_put(rt);

	for (i = 0; i < 100; i++) {
		rcu_read_lock();
		neigh = neigh_lookup(&arp_tbl, &gd->dst_ip, gd->ndev);
		if (neigh && (neigh->nud_state & NUD_VALID)) {
			memcpy(gd->dst_mac, neigh->ha, ETH_ALEN);
			neigh_release(neigh);
			rcu_read_unlock();
			return 0;
		}
		if (neigh) neigh_release(neigh);
		rcu_read_unlock();

		arp_send(ARPOP_REQUEST, ETH_P_ARP,
		         gd->dst_ip, gd->ndev,
		         gd->src_ip, NULL, gd->ndev->dev_addr, NULL);
		msleep(10);
	}

	dev_err(&gd->pdev->dev, "ARP failed for %pI4 on %s\n",
	        &gd->dst_ip, gd->cfg.ifname);
	return -EHOSTUNREACH;
}

static int gvsp_open_network(struct gvsp_dev *gd)
{
	struct in_device *in_dev;

	gd->ndev = dev_get_by_name(&init_net, gd->cfg.ifname);
	if (!gd->ndev) {
		dev_err(&gd->pdev->dev,
		        "netdev '%s' not found\n", gd->cfg.ifname);
		return -ENODEV;
	}

	rcu_read_lock();
	in_dev = __in_dev_get_rcu(gd->ndev);
	if (in_dev) {
		struct in_ifaddr *ifa;
		in_dev_for_each_ifa_rcu(ifa, in_dev) {
			gd->src_ip = ifa->ifa_local;
			break;
		}
	}
	rcu_read_unlock();

	if (!gd->src_ip) {
		dev_err(&gd->pdev->dev,
		        "No IPv4 address on %s\n", gd->cfg.ifname);
		dev_put(gd->ndev);
		gd->ndev = NULL;
		return -EADDRNOTAVAIL;
	}

	memcpy(gd->src_mac, gd->ndev->dev_addr, ETH_ALEN);
	gd->dst_ip   = gd->cfg.dst_ip;
	gd->src_port = gd->cfg.src_port ? gd->cfg.src_port : 50000u;
	gd->dst_port = gd->cfg.dst_port;

	if (gvsp_resolve_dst_mac(gd)) {
		dev_put(gd->ndev);
		gd->ndev = NULL;
		return -EHOSTUNREACH;
	}

	return 0;
}

static void gvsp_close_network(struct gvsp_dev *gd)
{
	if (gd->ndev) {
		dev_put(gd->ndev);
		gd->ndev = NULL;
	}
}

static inline void gvsp_fill_hdr(struct gvsp_hdr *h,
                                  u16 block_id, u8 fmt, u32 pkt_id)
{
	h->status    = 0;
	h->block_id  = htons(block_id);
	h->pkt_fmt   = fmt;
	h->pkt_id_hi = (pkt_id >> 16) & 0xFF;
	h->pkt_id    = htons(pkt_id & 0xFFFF);
}

static inline void gvsp_finalize_skb_headers(struct sk_buff *skb,
                                              u16 bid,
                                              size_t gvsp_body_len)
{
	struct udphdr *udph;
	struct iphdr  *iph;
	size_t udp_payload = sizeof(struct gvsp_hdr) + gvsp_body_len;
	size_t udp_len     = sizeof(struct udphdr) + udp_payload;
	size_t ip_len      = sizeof(struct iphdr)  + udp_len;

	udph = (struct udphdr *)(skb->data + ETH_HLEN + sizeof(struct iphdr));
	udph->len   = htons((u16)udp_len);
	udph->check = 0;

	iph = (struct iphdr *)(skb->data + ETH_HLEN);
	iph->tot_len = htons((u16)ip_len);
	iph->id      = htons(bid);
	iph->check   = 0;
	iph->check   = ip_fast_csum(iph, iph->ihl);
}

static int gvsp_send_leader(struct gvsp_dev *gd, u16 bid, ktime_t ts)
{
	struct sk_buff  *skb;
	struct gvsp_hdr *ghdr;
	size_t body_len = sizeof(struct gvsp_leader) - sizeof(struct gvsp_hdr);
	u8    *body;

	skb = gvsp_txring_get(gd);
	if (unlikely(!skb))
		return -EINTR;

	{
		int f;
		struct skb_shared_info *si = skb_shinfo(skb);
		for (f = 0; f < si->nr_frags; f++)
			put_page(skb_frag_page(&si->frags[f]));
		si->nr_frags = 0;
	}
	
	skb->data_len = 0;
	skb_shinfo(skb)->nr_frags = 0;
	skb->tail = (sk_buff_data_t)(skb->data + ETH_HLEN +
	             sizeof(struct iphdr) + sizeof(struct udphdr) +
	             sizeof(struct gvsp_hdr));
	skb->len  = ETH_HLEN + sizeof(struct iphdr) +
	            sizeof(struct udphdr) + sizeof(struct gvsp_hdr);

	ghdr = (struct gvsp_hdr *)(skb->data + ETH_HLEN +
	                            sizeof(struct iphdr) +
	                            sizeof(struct udphdr));
	gvsp_fill_hdr(ghdr, bid, GVSP_PKT_LEADER, 0);

	body = skb_put(skb, body_len);
	memset(body, 0, body_len);
	{
		struct {
			__be16 _reserved;
			__be16 payload_type;
			__be64 timestamp_ns;
			__be32 pixel_format;
			__be32 size_x;
			__be32 size_y;
			__be32 offset_x;
			__be32 offset_y;
			__be16 padding_x;
			__be16 padding_y;
		} __packed *lb = (void *)body;

		lb->payload_type = htons(GVSP_PAYLOAD_IMAGE);
		lb->timestamp_ns = cpu_to_be64((u64)ktime_to_ns(ts));
		lb->pixel_format = htonl(gd->cfg.pixel_format);
		lb->size_x       = htonl(gd->cfg.width);
		lb->size_y       = htonl(gd->cfg.height);
	}

	gvsp_finalize_skb_headers(skb, bid, body_len);

	skb_get(skb);
	dev_queue_xmit(skb);
	return 0;
}

static int gvsp_send_trailer(struct gvsp_dev *gd, u16 bid,
                              u32 last_pkt_id, u64 total_bytes)
{
	struct sk_buff  *skb;
	struct gvsp_hdr *ghdr;
	size_t body_len = sizeof(struct gvsp_trailer) - sizeof(struct gvsp_hdr);
	u8    *body;

	skb = gvsp_txring_get(gd);
	if (unlikely(!skb))
		return -EINTR;

	{
		int f;
		struct skb_shared_info *si = skb_shinfo(skb);
		for (f = 0; f < si->nr_frags; f++)
			put_page(skb_frag_page(&si->frags[f]));
		si->nr_frags = 0;
	}

	skb->data_len = 0;
	skb_shinfo(skb)->nr_frags = 0;
	skb->tail = (sk_buff_data_t)(skb->data + ETH_HLEN +
	             sizeof(struct iphdr) + sizeof(struct udphdr) +
	             sizeof(struct gvsp_hdr));
	skb->len  = ETH_HLEN + sizeof(struct iphdr) +
	            sizeof(struct udphdr) + sizeof(struct gvsp_hdr);

	ghdr = (struct gvsp_hdr *)(skb->data + ETH_HLEN +
	                            sizeof(struct iphdr) +
	                            sizeof(struct udphdr));
	gvsp_fill_hdr(ghdr, bid, GVSP_PKT_TRAILER, last_pkt_id + 1);

	body = skb_put(skb, body_len);
	{
		struct {
			__be32 payload_type;
			__be64 data_size;
		} __packed *tb = (void *)body;
		tb->payload_type = htonl(GVSP_PAYLOAD_IMAGE);
		tb->data_size    = cpu_to_be64(total_bytes);
	}

	gvsp_finalize_skb_headers(skb, bid, body_len);

	skb_get(skb);
	dev_queue_xmit(skb);
	return 0;
}

static int gvsp_send_payload(struct gvsp_dev *gd,
                             struct gvsp_buf *buf,
                             size_t offset, size_t chunk,
                             u16 bid, u32 pkt_id,
                             bool more)
{
	struct sk_buff  *skb;
	struct gvsp_hdr *ghdr;
	struct udphdr   *udph;
	struct iphdr    *iph;
	struct page     *page0, *page1;
	size_t           pg0_off, pg0_len, pg1_len;
	size_t           udp_payload, udp_len, ip_len;

	skb = gvsp_txring_get(gd);
	if (unlikely(!skb))
		return -EINTR;

	{
		int f;
		struct skb_shared_info *si = skb_shinfo(skb);
		for (f = 0; f < si->nr_frags; f++)
			put_page(skb_frag_page(&si->frags[f]));
		si->nr_frags = 0;
	}

	skb->data_len = 0;
	skb->len      = ETH_HLEN + sizeof(struct iphdr) +
	                sizeof(struct udphdr) + sizeof(struct gvsp_hdr);
	skb_shinfo(skb)->nr_frags = 0;

	ghdr = (struct gvsp_hdr *)(skb->data + ETH_HLEN +
	                            sizeof(struct iphdr) +
	                            sizeof(struct udphdr));
	ghdr->status   = 0;
	ghdr->block_id = htons(bid);
	ghdr->pkt_fmt  = GVSP_PKT_PAYLOAD;
	ghdr->pkt_id_hi = (pkt_id >> 16) & 0xFF;
	ghdr->pkt_id    = htons(pkt_id & 0xFFFF);

	pg0_off = offset_in_page(buf->paddr + offset);
	pg0_len = min(chunk, (size_t)(PAGE_SIZE - pg0_off));
	pg1_len = chunk - pg0_len;

	page0 = pfn_to_page(PHYS_PFN(buf->paddr + offset));

	if (unlikely(!pfn_valid(page_to_pfn(page0)))) {
		dev_err_ratelimited(&gd->pdev->dev,
		    "pfn invalid at buf+0x%zx\n", offset);
		return -EFAULT;
	}

	get_page(page0);
	skb_fill_page_desc(skb, 0, page0, pg0_off, pg0_len);
	skb->data_len += pg0_len;
	skb->len      += pg0_len;
	skb->truesize  = SKB_TRUESIZE(GVSP_HDR_ROOM) + chunk;

	if (pg1_len > 0) {
		page1 = pfn_to_page(PHYS_PFN(buf->paddr + offset + pg0_len));
		get_page(page1);
		skb_fill_page_desc(skb, 1, page1, 0, pg1_len);
		skb->data_len += pg1_len;
		skb->len      += pg1_len;
	}

	udp_payload = sizeof(struct gvsp_hdr) + chunk;
	udp_len     = sizeof(struct udphdr)   + udp_payload;
	ip_len      = sizeof(struct iphdr)    + udp_len;

	udph = (struct udphdr *)(skb->data + ETH_HLEN + sizeof(struct iphdr));
	udph->len   = htons((u16)udp_len);

	iph = (struct iphdr *)(skb->data + ETH_HLEN);
	iph->tot_len = htons((u16)ip_len);
	iph->id      = htons(bid);
	iph->check   = 0;
	iph->check   = ip_fast_csum(iph, iph->ihl);

	skb_get(skb);

	{
		struct netdev_queue *txq =
		    netdev_get_tx_queue(gd->ndev, 0);
		netdev_tx_t rc;
		int retries = 0;

		for (;;) {
			local_bh_disable();
			__netif_tx_lock(txq, smp_processor_id());

			if (likely(!netif_xmit_frozen_or_drv_stopped(txq))) {
				rc = netdev_start_xmit(skb, gd->ndev,
				                       txq, more);
				__netif_tx_unlock(txq);
				local_bh_enable();

				if (likely(rc == NETDEV_TX_OK))
					return 0;
			} else {
				__netif_tx_unlock(txq);
				local_bh_enable();
			}

			if (kthread_should_stop()) {
				kfree_skb(skb);
				return -EINTR;
			}
			if (++retries < 200)
				cpu_relax();
			else if (retries < 10000)
				usleep_range(1, 5);
			else {
				kfree_skb(skb);
				return -ENOBUFS;
			}
		}
	}
}

static int gvsp_tx_thread(void *data)
{
	struct gvsp_dev *gd = data;
	int              cur      = -1;
	bool             have_buf = false;

	sched_set_fifo(current);
	set_cpus_allowed_ptr(current, cpumask_of(1));

	if (gd->cfg.startup_delay_ms > 0) {
		msleep(gd->cfg.startup_delay_ms);
		if (kthread_should_stop())
			return 0;
	}

	while (!kthread_should_stop()) {

		unsigned long flags;
		int           new_ready = -1, i;
		u64           best_seq  = U64_MAX;
		int           wr_idx;

		spin_lock_irqsave(&gd->buf_lock, flags);

		wr_idx = gd->vdma_cur_wr;

		for (i = 0; i < N_BUF; i++) {
			if (i == wr_idx)
				continue;

			if (gd->bufs[i].state == BUF_READY &&
			    gd->bufs[i].seq < best_seq) {
				best_seq  = gd->bufs[i].seq;
				new_ready = i;
			}
		}

		if (new_ready >= 0) {
			if (have_buf && cur != new_ready)
				gd->bufs[cur].state = BUF_FREE;

			gd->bufs[new_ready].state = BUF_ETH_READING;
			cur      = new_ready;
			have_buf = true;
		}
		spin_unlock_irqrestore(&gd->buf_lock, flags);

		if (!have_buf) {
			int ret = wait_for_completion_interruptible_timeout(
			              &gd->frame_ready,
			              msecs_to_jiffies(2000));
			if (kthread_should_stop()) break;
			if (ret == 0)
				dev_warn_ratelimited(&gd->pdev->dev,
				    "Waiting for first frame (VDMA stall?)\n");
			continue;
		}

		{
			struct gvsp_buf *buf    = &gd->bufs[cur];
			size_t           total  = buf->size;
			size_t           offset = 0;
			u32              pkt_id = 1;
			ktime_t          ts     = ns_to_ktime(gd->bufs[cur].ts_ns);
			u16              bid    = ++gd->block_id;
			bool             aborted = false;
			int              ret;
			u64  local_pkts  = 0;
			u64  local_bytes = 0;
			u32  batch_cnt   = 0;

			ret = gvsp_send_leader(gd, bid, ts);
			if (ret) { aborted = true; goto frame_done; }
			local_pkts++;

			while (offset < total) {
				size_t chunk = min((size_t)gd->cfg.payload_size,
				                   total - offset);
				bool is_last_payload = (offset + chunk >= total);
				bool more;
				int xmit_ret;

				batch_cnt++;
				more = !is_last_payload &&
				       (batch_cnt % XMIT_BATCH_SIZE != 0);

				xmit_ret = gvsp_send_payload(gd, buf, offset,
				                             chunk, bid,
				                             pkt_id,
				                             more);
				if (xmit_ret == -EINTR)
					goto frame_aborted;
				if (xmit_ret == -ENOBUFS) {
					dev_warn_ratelimited(&gd->pdev->dev,
					    "xmit drop pkt %u frame %u\n",
					    pkt_id, bid);
				}

				local_pkts++;
				local_bytes += chunk;
				offset += chunk;
				pkt_id++;
			}

			ret = gvsp_send_trailer(gd, bid, pkt_id - 1, total);
			if (ret) { aborted = true; goto frame_done; }
			local_pkts++;

			goto frame_done;
frame_aborted:
			aborted = true;

frame_done:
			{
				unsigned long sflags;
				spin_lock_irqsave(&gd->stats_lock, sflags);
				gd->stats.packets_sent += local_pkts;
				gd->stats.bytes_sent   += local_bytes;
				if (!aborted) {
					gd->stats.frames_sent++;
					gd->stats.last_block_id = bid;
				}
				spin_unlock_irqrestore(&gd->stats_lock, sflags);
			}

			spin_lock_irqsave(&gd->buf_lock, flags);
			gd->bufs[cur].state = BUF_FREE;
			spin_unlock_irqrestore(&gd->buf_lock, flags);
			have_buf = false;
			cur      = -1;

			if (aborted) {
				dev_warn_ratelimited(&gd->pdev->dev,
				    "frame %u aborted, continuing\n", bid);
			}
		}

		reinit_completion(&gd->frame_ready);
		if (wait_for_completion_interruptible(&gd->frame_ready) < 0)
			break;
	}

	if (have_buf) {
		unsigned long flags;
		spin_lock_irqsave(&gd->buf_lock, flags);
		gd->bufs[cur].state = BUF_FREE;
		spin_unlock_irqrestore(&gd->buf_lock, flags);
	}

	return 0;
}

static size_t gvsp_frame_size(u32 pixel_format, u32 width, u32 height)
{
	size_t npix;

	if (width > GVSP_MAX_DIM || height > GVSP_MAX_DIM)
		return 0;
	npix = (size_t)width * (size_t)height;

	switch (pixel_format) {
	case GVSP_PIX_MONO8:
	case GVSP_PIX_BAYERRG8:
	case GVSP_PIX_BAYERBG8:
	case GVSP_PIX_BAYERGR8:
	case GVSP_PIX_BAYERGB8:
		return npix;

	case GVSP_PIX_MONO12P:
	case GVSP_PIX_BAYERRG12P:
	case GVSP_PIX_BAYERBG12P:
	case GVSP_PIX_BAYERGR12P:
	case GVSP_PIX_BAYERGB12P:
		return npix * 3 / 2;

	case GVSP_PIX_BAYERRG14P:
	case GVSP_PIX_BAYERBG14P:
	case GVSP_PIX_BAYERGR14P:
	case GVSP_PIX_BAYERGB14P:
		return npix * 7 / 4;

	case GVSP_PIX_BAYERRG10P:
	case GVSP_PIX_BAYERBG10P:
	case GVSP_PIX_BAYERGR10P:
	case GVSP_PIX_BAYERGB10P:
		return npix * 5 / 4;

	case GVSP_PIX_BAYERRG16:
	case GVSP_PIX_BAYERBG16:
	case GVSP_PIX_BAYERGR16:
	case GVSP_PIX_BAYERGB16:
		return npix * 2;

	case GVSP_PIX_MONO12:
	case GVSP_PIX_BAYERRG10:
	case GVSP_PIX_BAYERRG12:
	case GVSP_PIX_BAYERRG14:
	case GVSP_PIX_BAYERBG10:
	case GVSP_PIX_BAYERBG12:
	case GVSP_PIX_BAYERBG14:
	case GVSP_PIX_BAYERGR10:
	case GVSP_PIX_BAYERGR12:
	case GVSP_PIX_BAYERGR14:
	case GVSP_PIX_BAYERGB10:
	case GVSP_PIX_BAYERGB12:
	case GVSP_PIX_BAYERGB14:
	case GVSP_PIX_RGB565P:
	case GVSP_PIX_BGR565P:
	case GVSP_PIX_YCBCR422_8:
	case GVSP_PIX_YUV422_8:
		return npix * 2;

	case GVSP_PIX_RGB8:
	case GVSP_PIX_BGR8:
		return npix * 3;

	case GVSP_PIX_BGRA8:
		return npix * 4;

	default:
		return 0;
	}
}

static int gvsp_stream_start(struct gvsp_dev *gd,
                              struct gvsp_stream_cfg __user *ucfg)
{
	struct gvsp_stream_cfg cfg;
	size_t frame_sz;
	int  ret, i;

	if (copy_from_user(&cfg, ucfg, sizeof(cfg)))
		return -EFAULT;
	if (!cfg.width || !cfg.height || !cfg.dst_ip || !cfg.dst_port)
		return -EINVAL;

	if (cfg.payload_size == 0 || cfg.payload_size > GVSP_MAX_PAYLOAD)
		cfg.payload_size = GVSP_MAX_PAYLOAD;

	if (cfg.ts_clock > GVSP_TS_CLOCK_PHC)
		return -EINVAL;
	if (memchr_inv(cfg._pad, 0, sizeof(cfg._pad)))
		return -EINVAL;

	frame_sz = gvsp_frame_size(cfg.pixel_format, cfg.width, cfg.height);
	if (frame_sz == 0 || frame_sz > GVSP_MAX_FRAME_BYTES)
		return -EINVAL;
	if (DIV_ROUND_UP(frame_sz, cfg.payload_size) + 1 > GVSP_PKT_ID_MAX)
		return -EINVAL;

	spin_lock(&gd->cfg_lock);
	if (gd->streaming) { spin_unlock(&gd->cfg_lock); return -EBUSY; }
	memcpy(&gd->cfg, &cfg, sizeof(cfg));
	spin_unlock(&gd->cfg_lock);

	{
		unsigned long bflags;
		spin_lock_irqsave(&gd->buf_lock, bflags);
		for (i = 0; i < N_BUF; i++) {
			gd->bufs[i].state   = BUF_FREE;
			gd->bufs[i].seq     = 0;
		}
		gd->vdma_cur_wr     = 0;
		gd->eth_read_idx    = -1;
		gd->ready_idx       = -1;
		gd->vdma_seq        = 0;
		spin_unlock_irqrestore(&gd->buf_lock, bflags);
	}

	ret = gvsp_alloc_bufs(gd, frame_sz);
	if (ret) return ret;

	ret = gvsp_open_network(gd);
	if (ret) goto err_bufs;

	if (cfg.ts_clock == GVSP_TS_CLOCK_PHC) {
		u64 phc_ns;

		ret = macb_zynq_ptp_now_ns(gd->ndev, &phc_ns);
		if (ret) {
			dev_err(&gd->pdev->dev,
				"PTP timestamps requested but %s has no PTP clock running (%d)\n",
				cfg.ifname, ret);
			ret = -ENODEV;
			goto err_net;
		}
	}

	ret = gvsp_alloc_tx_ring(gd);
	if (ret) goto err_net;

	init_completion(&gd->frame_ready);

	ret = gvsp_start_vdma(gd);
	if (ret) goto err_txring;

	gd->tx_thread = kthread_run(gvsp_tx_thread, gd,
	                             "gvsp_tx/%d", gd->minor);
	if (IS_ERR(gd->tx_thread)) {
		ret = PTR_ERR(gd->tx_thread);
		gd->tx_thread = NULL;
		goto err_vdma;
	}

	spin_lock(&gd->cfg_lock);
	gd->streaming = true;
	spin_unlock(&gd->cfg_lock);

	return 0;

err_vdma:   gvsp_stop_vdma(gd);
err_txring: gvsp_free_tx_ring(gd);
err_net:    gvsp_close_network(gd);
err_bufs:   gvsp_free_bufs(gd);
	return ret;
}

static int gvsp_stream_stop(struct gvsp_dev *gd)
{
	spin_lock(&gd->cfg_lock);
	if (!gd->streaming) { spin_unlock(&gd->cfg_lock); return 0; }
	gd->streaming = false;
	spin_unlock(&gd->cfg_lock);

	if (gd->tx_thread) {
		complete(&gd->frame_ready);
		struct task_struct *t = gd->tx_thread;
		gd->tx_thread = NULL;
		kthread_stop(t);
	}

	gvsp_stop_vdma(gd);
	gvsp_free_tx_ring(gd);
	gvsp_close_network(gd);
	gvsp_free_bufs(gd);

	{
		unsigned long bflags;
		spin_lock_irqsave(&gd->buf_lock, bflags);
		gd->eth_read_idx    = -1;
		gd->ready_idx       = -1;
		gd->vdma_cur_wr     = 0;
		gd->vdma_seq        = 0;
		spin_unlock_irqrestore(&gd->buf_lock, bflags);
	}

	gd->frame_log_pos = 0;

	return 0;
}

static long gvsp_ioctl(struct file *filp, unsigned int cmd,
                        unsigned long arg)
{
	struct gvsp_dev *gd = filp->private_data;
	struct gvsp_stats stats;
	int ret = 0;

	switch (cmd) {
	case GVSP_IOC_START:
		if (mutex_lock_interruptible(&gd->ctl_lock))
			return -ERESTARTSYS;
		ret = gvsp_stream_start(gd,
		          (struct gvsp_stream_cfg __user *)arg);
		mutex_unlock(&gd->ctl_lock);
		break;
	case GVSP_IOC_STOP:
		if (mutex_lock_interruptible(&gd->ctl_lock))
			return -ERESTARTSYS;
		ret = gvsp_stream_stop(gd);
		mutex_unlock(&gd->ctl_lock);
		break;
	case GVSP_IOC_GET_STATS:
		{
			unsigned long sflags;
			spin_lock_irqsave(&gd->stats_lock, sflags);
			memcpy(&stats, &gd->stats, sizeof(stats));
			spin_unlock_irqrestore(&gd->stats_lock, sflags);
		}
		if (copy_to_user((void __user *)arg, &stats, sizeof(stats)))
			ret = -EFAULT;
		break;
	case GVSP_IOC_SET_TS_ZERO: {
		unsigned long tflags;
		u64 zero;

		if (copy_from_user(&zero, (void __user *)arg, sizeof(zero)))
			return -EFAULT;
		if (zero > ktime_get_ns())
			return -EINVAL;
		spin_lock_irqsave(&gd->ts_lock, tflags);
		gd->ts_zero_ns = zero;
		spin_unlock_irqrestore(&gd->ts_lock, tflags);
		break;
	}
	case GVSP_IOC_RESET:
		if (mutex_lock_interruptible(&gd->ctl_lock))
			return -ERESTARTSYS;
		ret = gvsp_stream_stop(gd);
		if (!ret) {
			unsigned long sflags;
			spin_lock_irqsave(&gd->stats_lock, sflags);
			memset(&gd->stats, 0, sizeof(gd->stats));
			spin_unlock_irqrestore(&gd->stats_lock, sflags);
			gd->block_id = 0;
		}
		mutex_unlock(&gd->ctl_lock);
		break;
	default:
		ret = -ENOTTY;
	}
	return ret;
}

static int gvsp_open(struct inode *inode, struct file *filp)
{
	filp->private_data = container_of(inode->i_cdev,
	                                   struct gvsp_dev, cdev);
	return 0;
}

static int gvsp_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations gvsp_fops = {
	.owner          = THIS_MODULE,
	.open           = gvsp_open,
	.release        = gvsp_release,
	.unlocked_ioctl = gvsp_ioctl,
};

static int gvsp_probe(struct platform_device *pdev)
{
	struct gvsp_dev *gd;
	int minor, ret;

	mutex_lock(&gvsp_devs_lock);
	for (minor = 0; minor < GVSP_MAX_DEVS; minor++)
		if (!gvsp_devs[minor]) break;
	if (minor == GVSP_MAX_DEVS) {
		mutex_unlock(&gvsp_devs_lock);
		return -ENODEV;
	}

	gd = devm_kzalloc(&pdev->dev, sizeof(*gd), GFP_KERNEL);
	if (!gd) { mutex_unlock(&gvsp_devs_lock); return -ENOMEM; }

	gd->pdev          = pdev;
	gd->minor         = minor;
	gd->eth_read_idx  = -1;
	gd->ready_idx     = -1;

	spin_lock_init(&gd->buf_lock);
	spin_lock_init(&gd->cfg_lock);
	spin_lock_init(&gd->stats_lock);
	spin_lock_init(&gd->ts_lock);
	mutex_init(&gd->ctl_lock);
	init_completion(&gd->frame_ready);

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret) {
		dev_err(&pdev->dev,
		        "dma_set_mask_and_coherent(32): %d\n", ret);
		mutex_unlock(&gvsp_devs_lock);
		return ret;
	}

	cdev_init(&gd->cdev, &gvsp_fops);
	gd->cdev.owner = THIS_MODULE;
	ret = cdev_add(&gd->cdev, MKDEV(MAJOR(gvsp_devt), minor), 1);
	if (ret) { mutex_unlock(&gvsp_devs_lock); return ret; }

	gd->chr_dev = device_create(gvsp_class, &pdev->dev,
	                             MKDEV(MAJOR(gvsp_devt), minor),
	                             gd, "gvsp%d", minor);
	if (IS_ERR(gd->chr_dev)) {
		ret = PTR_ERR(gd->chr_dev);
		cdev_del(&gd->cdev);
		mutex_unlock(&gvsp_devs_lock);
		return ret;
	}

	gvsp_devs[minor] = gd;
	platform_set_drvdata(pdev, gd);
	mutex_unlock(&gvsp_devs_lock);

	dev_info(&pdev->dev,
	         "gvsp%d ready  zero-copy  N_BUF=%d\n",
	         minor, N_BUF);
	return 0;
}

static int gvsp_remove(struct platform_device *pdev)
{
	struct gvsp_dev *gd = platform_get_drvdata(pdev);

	mutex_lock(&gd->ctl_lock);
	gvsp_stream_stop(gd);
	mutex_unlock(&gd->ctl_lock);

	mutex_lock(&gvsp_devs_lock);
	device_destroy(gvsp_class, MKDEV(MAJOR(gvsp_devt), gd->minor));
	cdev_del(&gd->cdev);
	gvsp_devs[gd->minor] = NULL;
	mutex_unlock(&gvsp_devs_lock);
	dev_info(&pdev->dev, "gvsp%d removed\n", gd->minor);
	return 0;
}

static const struct of_device_id vdma_gigev_of_match[] = {
	{ .compatible = "xlnx,gvsp-tx-1.00.a" },
	{}
};
MODULE_DEVICE_TABLE(of, vdma_gigev_of_match);

static struct platform_driver vdma_gigev_driver = {
	.probe  = gvsp_probe,
	.remove = gvsp_remove,
	.driver = {
		.name           = DRIVER_NAME,
		.of_match_table = vdma_gigev_of_match,
	},
};

static int __init vdma_gigev_init(void)
{
	int ret = alloc_chrdev_region(&gvsp_devt, 0,
	                               GVSP_MAX_DEVS, DRIVER_NAME);
	if (ret) return ret;

	gvsp_class = class_create(DRIVER_NAME);
	if (IS_ERR(gvsp_class)) {
		unregister_chrdev_region(gvsp_devt, GVSP_MAX_DEVS);
		return PTR_ERR(gvsp_class);
	}

	ret = platform_driver_register(&vdma_gigev_driver);
	if (ret) {
		class_destroy(gvsp_class);
		unregister_chrdev_region(gvsp_devt, GVSP_MAX_DEVS);
		return ret;
	}

	pr_info("GigE Vision: GVSP TX loaded  N_BUF=%d  MAX_PAYLOAD=%u\n",
	        N_BUF, GVSP_MAX_PAYLOAD);
	return 0;
}

static void __exit vdma_gigev_exit(void)
{
	platform_driver_unregister(&vdma_gigev_driver);
	class_destroy(gvsp_class);
	unregister_chrdev_region(gvsp_devt, GVSP_MAX_DEVS);
	pr_info("GigE Vision: GVSP TX unloaded\n");
}

module_init(vdma_gigev_init);
module_exit(vdma_gigev_exit);

MODULE_AUTHOR("Gaurav Singh <gauravsingh@circuitvalley.com>");
MODULE_DESCRIPTION("CHC5 GigE Vision stream transmitter");
MODULE_LICENSE("GPL");
