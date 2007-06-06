// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2004-2006 Sage Weil <sage@newdream.net>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */

#ifndef __REPLICATEDPG_H
#define __REPLICATEDPG_H


#include "PG.h"

#include "messages/MOSDOp.h"


class ReplicatedPG : public PG {
public:  
  /*
   * gather state on the primary/head while replicating an osd op.
   */
  class RepGather {
  public:
    class MOSDOp *op;
    tid_t rep_tid;

    ObjectStore::Transaction t;
    bool applied;

    set<int>  waitfor_ack;
    set<int>  waitfor_commit;
    
    utime_t   start;

    bool sent_ack, sent_commit;
    
    set<int>         osds;
    eversion_t       new_version;

    eversion_t       pg_local_last_complete;
    map<int,eversion_t> pg_complete_thru;
    
    RepGather(MOSDOp *o, tid_t rt, eversion_t nv, eversion_t lc) :
      op(o), rep_tid(rt),
      applied(false),
      sent_ack(false), sent_commit(false),
      new_version(nv), 
      pg_local_last_complete(lc) { }

    bool can_send_ack() { 
      return !sent_ack && !sent_commit &&
        waitfor_ack.empty(); 
    }
    bool can_send_commit() { 
      return !sent_commit &&
        waitfor_ack.empty() && waitfor_commit.empty(); 
    }
    bool can_delete() { 
      return waitfor_ack.empty() && waitfor_commit.empty(); 
    }
  };

protected:
  // replica ops
  // [primary|tail]
  map<tid_t, RepGather*>               rep_gather;
  map<tid_t, list<class Message*> > waiting_for_repop;

  // load balancing
  set<object_t> balancing_reads;
  set<object_t> unbalancing_reads;
  hash_map<object_t, list<Message*> > waiting_for_balanced_reads;
  hash_map<object_t, list<Message*> > waiting_for_unbalanced_reads;  // i.e. primary-lock

  void get_rep_gather(RepGather*);
  void apply_repop(RepGather *repop);
  void put_rep_gather(RepGather*);
  void issue_repop(MOSDOp *op, int osd);
  RepGather *new_rep_gather(MOSDOp *op);
  void repop_ack(RepGather *repop,
                 int result, bool commit,
                 int fromosd, eversion_t pg_complete_thru=0);
  
  // push/pull
  int num_pulling;
  
  void push(object_t oid, int dest);
  void pull(object_t oid);

  // modify
  objectrev_t assign_version(MOSDOp *op);
  void op_modify_commit(tid_t rep_tid, eversion_t pg_complete_thru);
  void op_rep_modify_commit(MOSDOp *op, int ackerosd, eversion_t last_complete);

  void prepare_log_transaction(ObjectStore::Transaction& t, 
			       MOSDOp *op, eversion_t& version, 
			       objectrev_t crev, objectrev_t rev,
			       eversion_t trim_to);
  void prepare_op_transaction(ObjectStore::Transaction& t, 
			      MOSDOp *op, eversion_t& version, 
			      objectrev_t crev, objectrev_t rev);

  friend class C_OSD_ModifyCommit;
  friend class C_OSD_RepModifyCommit;


  // pg on-disk content
  void clean_up_local(ObjectStore::Transaction& t);

  void cancel_recovery();
  bool do_recovery();
  void do_peer_recovery();

  void clean_replicas();


  void op_read(MOSDOp *op);
  void op_modify(MOSDOp *op);
  void op_rep_modify(MOSDOp *op);
  void op_push(MOSDOp *op);
  void op_pull(MOSDOp *op);




public:
  ReplicatedPG(OSD *o, pg_t p) : 
    PG(o,p),
    num_pulling(0)
  { }
  ~ReplicatedPG() {}

  bool preprocess_op(MOSDOp *op);
  void do_op(MOSDOp *op);
  void do_op_reply(MOSDOpReply *r);

  bool same_for_read_since(epoch_t e);
  bool same_for_modify_since(epoch_t e);
  bool same_for_rep_modify_since(epoch_t e);

  bool is_missing_object(object_t oid);
  void wait_for_missing_object(object_t oid, MOSDOp *op);

  void note_failed_osd(int o);
  void on_acker_change();
  void on_role_change();

};


inline ostream& operator<<(ostream& out, ReplicatedPG::RepGather& repop)
{
  out << "repgather(" << &repop << " rep_tid=" << repop.rep_tid 
      << " wfack=" << repop.waitfor_ack
      << " wfcommit=" << repop.waitfor_commit;
  out << " pct=" << repop.pg_complete_thru;
  out << " op=" << *(repop.op);
  out << " repop=" << &repop;
  out << ")";
  return out;
}


#endif
