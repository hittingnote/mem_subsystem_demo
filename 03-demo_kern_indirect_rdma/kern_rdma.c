#include <linux/list.h>
#include <linux/slab.h>
#include <linux/rwlock.h>
#include <rdma/ib_verbs.h>
#include <rdma/ib_cache.h>
#include "kern_rdma.h"
#include "kern_sg.h"
#include "common.h"

static struct list_head ib_dev_list;
static rwlock_t rwlock;

struct ib_dev_ent {
	struct ib_device				*device;
	struct list_head				ent;
};

struct rdma_conn_param {
	u32					qpn;
	u32					psn;
	u16					lid;
	union ib_gid		gid;
};

static struct ib_client init_ibdev_client;

static int setup_connection(bool is_server, const struct sockaddr_in *s_addr,
			struct socket **sock, struct socket **client_sock) {
	struct socket **create_sock;
	int flag = 1;
	int err = 0;

	*sock = (struct socket*)kzalloc(sizeof(struct socket), GFP_KERNEL);
	*client_sock = (struct socket*)kzalloc(sizeof(struct socket), GFP_KERNEL);
	if(!(*sock) || !(*client_sock)) {
		err = -ENOMEM;
		goto err_kzalloc;
	}

	create_sock = (is_server)? sock: client_sock;

	err = sock_create_kern(&init_net, AF_INET, SOCK_STREAM, 0, create_sock);
	if(err) {
		err_info("socket create error\n");
		goto err_socket;
	}

	err = kernel_setsockopt(*create_sock, SOL_SOCKET, SO_REUSEPORT,
				(char*)&flag, sizeof(int));
	if(err) {
		err_info("setsockopt error\n");
		goto err_socket;
	}

	if(is_server) {
		err = kernel_bind(*sock, (struct sockaddr*)s_addr, sizeof(*s_addr));
		if(err < 0) {
			err_info("bind error\n");
			goto out_sock_release;
		}

		err = kernel_listen(*sock, 10);
		if(err < 0) {
			err_info("listen error\n");
			goto out_sock_release;
		}

		err = kernel_accept(*sock, client_sock, 0);
		if(err < 0) {
			err_info("accept error\n");
			goto out_sock_release;
		}
	}
	else {
		err = kernel_connect(*client_sock, (struct sockaddr*)s_addr,
						sizeof(*s_addr), 0);
		if(err) {
			err_info("kernel_connect error\n");
			goto out_sock_release;
		}
	}

	return 0;

out_sock_release:
	sock_release(*create_sock);
	return err;
err_socket:
	kfree(*sock);
	kfree(*client_sock);
err_kzalloc:
	return err;
}

static int exchange_info(bool is_server, struct rdma_conn_param *local,
			struct rdma_conn_param *remote,
			struct socket *sock, struct socket *client_sock) {
	struct kvec vec;
	struct msghdr msg;
	int err = 0, i;

	for(i = 0; i < 2; i++) {
		memset(&vec, 0, sizeof(vec));
		memset(&msg, 0, sizeof(msg));
		if((i % 2) ^ is_server) {
			vec.iov_base = remote;
			vec.iov_len = sizeof(*remote);
			err = kernel_recvmsg(client_sock, &msg, &vec, 1, sizeof(*remote), 0);
		}
		else {
			vec.iov_base = local;
			vec.iov_len = sizeof(*local);
			err = kernel_sendmsg(client_sock, &msg, &vec, 1, sizeof(*local));
		}

		if(err < sizeof(*local)) {
			err_info("msg error\n");
			err = -EPIPE;
			goto err_msg;
		}
		else {
			err = 0;
		}
	}

err_msg:
	return err;
}

static void close_connection(bool is_server,
			struct socket *sock, struct socket *client_sock) {
	sock_release(client_sock);
	if(is_server)
		sock_release(sock);
//	kfree(client_sock);
//	kfree(sock);
}

static void add_device_to_list(struct ib_device *ibdev) {
	struct ib_dev_ent *dev_ent = NULL;
	dev_ent = kzalloc(sizeof(*dev_ent), GFP_KERNEL);
	if(!dev_ent) {
		err_info("Failed to alloc new entry for ib_device\n");
		return;
	}

	dev_ent->device = ibdev;
	write_lock(&rwlock);
	list_add_tail(&dev_ent->ent, &ib_dev_list);
	ib_set_client_data(ibdev, &init_ibdev_client, dev_ent);
	write_unlock(&rwlock);
}

static void rm_device_from_list(struct ib_device *ibdev, void *data) {
	struct ib_dev_ent *dev_ent = (struct ib_dev_ent*)data;
	write_lock(&rwlock);
	list_del(&dev_ent->ent);
	write_unlock(&rwlock);
	kfree(dev_ent);
}

static struct ib_client init_ibdev_client = {
	.name				= "init_ibdev_list",
	.add				= add_device_to_list,
	.remove				= rm_device_from_list,
};

static struct ib_device *get_ib_dev_from_name(const char *devname) {
	struct ib_device *ibdev = NULL;
	struct ib_dev_ent *ibdev_ent;

	read_lock(&rwlock);
	list_for_each_entry(ibdev_ent, &ib_dev_list, ent) {
		if(!strcmp(ibdev_ent->device->name, devname)) {
			ibdev = ibdev_ent->device;
			goto out;
		}
	}

out:
	read_unlock(&rwlock);
	return ibdev;
}

int init_ib_dev_list(void) {
	int err = 0;
	INIT_LIST_HEAD(&ib_dev_list);
	rwlock_init(&rwlock);

	err = ib_register_client(&init_ibdev_client);
	if(err) {
		err_info("Failed to register ib_client\n");
		return err;
	}

	return err;
}

void destroy_ib_dev_list(void) {
	ib_unregister_client(&init_ibdev_client);
}

int kern_rdma_core(bool is_server, const char *dev_name,
				const struct sockaddr_in *s_addr, 
				int rdma_port, int sgid_index,
				unsigned long virtaddr, unsigned long length) {
	int err = 0;
	struct ib_device *ib_dev;
	struct ib_pd *pd;
	struct ib_mr *mr;
	struct ib_cq *cq;
	struct ib_qp *qp;
	struct ib_cq_init_attr cq_init_attr = {};
	struct ib_qp_init_attr qp_init_attr;
	struct ib_qp_attr qp_attr;
	struct ib_port_attr port_attr;
	struct ib_ah *ah;
	union ib_gid local_gid;
	struct rdma_conn_param local_info, remote_info;
	struct ib_sge sge_list;
	struct ib_recv_wr recv_wr, *bad_recv_wr;
	struct ib_send_wr send_wr, *bad_send_wr;
	struct socket *sock, *client_sock;
	struct sg_table *sgtbl;
	u64 dma_addr;
	int attr_flag;

	ib_dev = get_ib_dev_from_name(dev_name);
	if(!ib_dev) {
		return -ENODEV;
	}

	pd = ib_alloc_pd(ib_dev, 0);
	if(IS_ERR(pd)) {
		err = (int)PTR_ERR(pd);
		goto err_alloc_pd;
	}

	err = get_sg_list(virtaddr, length,
			dma_get_max_seg_size(ib_dev->dma_device), &sgtbl);
	if(err) {
		err_info("Failed to get sg list\n");
		goto err_sg_list;
	}

	mr = ib_get_dma_mr(pd, IB_ACCESS_LOCAL_WRITE);
	if(IS_ERR(mr)) {
		err = (int)PTR_ERR(mr);
		goto err_get_dma_mr;
	}

	err = ib_dma_map_sg(ib_dev, sgtbl->sgl,
					sgtbl->nents, DMA_BIDIRECTIONAL);
	if(err <= 0) {
		err_info("Failed to map DMA\n");
		err = -EFAULT;
		goto err_dma_map_single;
	}
	else
		err = 0;

	dma_addr = get_dma_address_from_sgtbl(sgtbl, virtaddr);

	cq_init_attr.cqe = 1;
	cq_init_attr.comp_vector = 0;
	cq_init_attr.flags = 0;
	cq = ib_create_cq(ib_dev, NULL, NULL,
					NULL, &cq_init_attr);
	if(IS_ERR(cq)) {
		err = -ENODEV;
		goto err_create_cq;
	}

	memset(&qp_init_attr, 0, sizeof(qp_init_attr));
	qp_init_attr.send_cq = cq;
	qp_init_attr.recv_cq = cq;
	qp_init_attr.cap.max_send_wr = 128;
	qp_init_attr.cap.max_recv_wr = 128;
	qp_init_attr.cap.max_send_sge = 30;
	qp_init_attr.cap.max_recv_sge = 30;
	qp_init_attr.qp_type = IB_QPT_RC;
	qp_init_attr.sq_sig_type = IB_SIGNAL_ALL_WR;
	qp = ib_create_qp(pd, &qp_init_attr);
	if(IS_ERR(qp)) {
		err = -ENODEV;
		goto err_create_qp;
	}

	err = ib_query_port(ib_dev, rdma_port, &port_attr);
	if(err) {
		goto err_conn;
	}

	err = rdma_query_gid(ib_dev, rdma_port, sgid_index, &local_gid);
	if(err) {
		goto err_conn;
	}
	dbg_info("server: %d, local gid: %pI4\n", is_server, local_gid.raw+12);

	local_info.qpn = qp->qp_num;
	local_info.psn = 0;
	local_info.lid = port_attr.lid;
	memcpy(&local_info.gid, &local_gid, sizeof(local_gid));

	err = setup_connection(is_server, s_addr, &sock, &client_sock);
	if(err) {
		err_info("setup_connection error\n");
		goto err_conn;
	}

	err = exchange_info(is_server, &local_info, &remote_info, sock, client_sock);
	if(err) {
		err_info("exchange_info error\n");
		goto err_modify_qp;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IB_QPS_INIT;
	qp_attr.pkey_index = 0;
	qp_attr.port_num = rdma_port;
	qp_attr.qp_access_flags = IB_ACCESS_LOCAL_WRITE;
	attr_flag = (IB_QP_STATE | IB_QP_PKEY_INDEX |
				IB_QP_PORT | IB_QP_ACCESS_FLAGS);
	err = ib_modify_qp(qp, &qp_attr, attr_flag);
	if(err) {
		goto err_modify_qp;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IB_QPS_RTR;
	qp_attr.path_mtu = IB_MTU_1024;
	qp_attr.dest_qp_num = remote_info.qpn;
	qp_attr.rq_psn = remote_info.psn;
	qp_attr.max_dest_rd_atomic = 1;
	qp_attr.min_rnr_timer = 12;
	qp_attr.ah_attr.ib.dlid = remote_info.lid;
	qp_attr.ah_attr.sl = 0;
	qp_attr.ah_attr.ib.src_path_bits = 0;
	qp_attr.ah_attr.port_num = rdma_port;
	qp_attr.ah_attr.ah_flags = IB_AH_GRH;
	qp_attr.ah_attr.grh.hop_limit = 255;
	memcpy(&qp_attr.ah_attr.grh.dgid, &remote_info.gid,
							sizeof(remote_info.gid));
	qp_attr.ah_attr.grh.sgid_index = sgid_index;
	qp_attr.ah_attr.grh.traffic_class = 0;
	qp_attr.ah_attr.type = RDMA_AH_ATTR_TYPE_ROCE;

	attr_flag = (IB_QP_STATE | IB_QP_AV | IB_QP_PATH_MTU |
				IB_QP_DEST_QPN | IB_QP_RQ_PSN |
			IB_QP_MAX_DEST_RD_ATOMIC | IB_QP_MIN_RNR_TIMER);

	ah = rdma_create_user_ah(qp->pd, &qp_attr.ah_attr, NULL);
	if(IS_ERR(ah)) {
		goto err_modify_qp;
	}
	
	err = rdma_destroy_ah(ah, 0);
	if(err) {
		goto err_modify_qp;
	}

	err = ib_modify_qp(qp, &qp_attr, attr_flag);
	if(err) {
		goto err_modify_qp;
	}

	memset(&qp_attr, 0, sizeof(qp_attr));
	qp_attr.qp_state = IB_QPS_RTS;
	qp_attr.timeout = 14;
	qp_attr.retry_cnt = 7;
	qp_attr.rnr_retry = 7;
	qp_attr.sq_psn = local_info.psn;
	qp_attr.max_rd_atomic = 1;

	attr_flag = (IB_QP_STATE | IB_QP_TIMEOUT | IB_QP_RETRY_CNT |
			IB_QP_RNR_RETRY | IB_QP_SQ_PSN | IB_QP_MAX_QP_RD_ATOMIC);

	err = ib_modify_qp(qp, &qp_attr, attr_flag);
	if(err) {
		goto err_modify_qp;
	}

	sge_list.addr = (uintptr_t)dma_addr;
	sge_list.length = length;
	sge_list.lkey = mr->lkey;

	if(is_server) {
		recv_wr.wr_id = 1;
		recv_wr.sg_list = &sge_list;
		recv_wr.next = NULL;
		recv_wr.num_sge = 1;
		err = ib_post_recv(qp, &recv_wr, (const struct ib_recv_wr**)&bad_recv_wr);
		if(err) {
			goto err_modify_qp;
		}
	}
	else {
		send_wr.wr_id = 1;
		send_wr.sg_list = &sge_list;
		send_wr.next = NULL;
		send_wr.num_sge = 1;
		send_wr.opcode = IB_WR_SEND;
		send_wr.send_flags = IB_SEND_SIGNALED;
		err = ib_post_send(qp, &send_wr, (const struct ib_send_wr**)&bad_send_wr);
		if(err) {
			goto err_modify_qp;
		}
	}

	while(1) {
		struct ib_wc wc;
		int ne = ib_poll_cq(cq, 1, &wc);
		if(ne < 0) {
			err = -EFAULT;
			err_info("ib_poll_cq error\n");
			goto err_modify_qp;
		}

		if(ne > 0) {
			if(wc.status != IB_WC_SUCCESS) {
				err_info("server: %d, wc not success\n", is_server);
				goto err_modify_qp;
			}
			break;
		}

		err = exchange_info(is_server, &local_info, &remote_info,
						sock, client_sock);
		if(err) {
			if(err != -EPIPE) {
				err_info("server: %d, Remote peer has closed connection\n", is_server);
			}
			else {
				err = 0;
			}
			goto err_modify_qp;
		}
	}

err_modify_qp:
	close_connection(is_server, sock, client_sock);
err_conn:
	ib_destroy_qp(qp);
err_create_qp:
	ib_destroy_cq(cq);
err_create_cq:
	ib_dma_unmap_sg(ib_dev, sgtbl->sgl,
				sgtbl->nents, DMA_BIDIRECTIONAL);
err_dma_map_single:
	ib_dereg_mr(mr);
err_get_dma_mr:
	if(err)
		free_sg_list(sgtbl);
err_sg_list:
	ib_dealloc_pd(pd);
err_alloc_pd:
	return err;
}

void kern_rdma_release(void) {
	struct sg_table *sgtbl =
				get_sg_tbl_from_pid(current->pid);
	if(!sgtbl)
		return;
	free_sg_list(sgtbl);
}
