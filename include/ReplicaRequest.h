/* -------------------------------------------------------------------------- */
/* Copyright 2002-2018, OpenNebula Project, OpenNebula Systems                */
/*                                                                            */
/* Licensed under the Apache License, Version 2.0 (the "License"); you may    */
/* not use this file except in compliance with the License. You may obtain    */
/* a copy of the License at                                                   */
/*                                                                            */
/* http://www.apache.org/licenses/LICENSE-2.0                                 */
/*                                                                            */
/* Unless required by applicable law or agreed to in writing, software        */
/* distributed under the License is distributed on an "AS IS" BASIS,          */
/* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   */
/* See the License for the specific language governing permissions and        */
/* limitations under the License.                                             */
/* -------------------------------------------------------------------------- */

#ifndef REPLICA_REQUEST_H_
#define REPLICA_REQUEST_H_

#include "SyncRequest.h"

/**
 * This class represents a log entry replication request. The replication request
 * is synchronous: once it has been replicated in a majority of followers the
 * client is notified (SqlDB::exec_wr() call) and DB updated.
 */
class ReplicaRequest : public SyncRequest
{
public:
    ReplicaRequest(unsigned int i):_index(i), _to_commit(-1), _replicas(1){};

    ~ReplicaRequest(){};

    /**
     *  This function updates the number of replicas of the record and decrement
     *  the number of servers left to reach majority consensus. If it reaches 0,
     *  the client is notified
     *      @return number of replicas for this log
     */
    int add_replica()
    {
        int __replicas;

        _replicas++;

        if ( _to_commit > 0 )
        {
            _to_commit--;
        }

        __replicas = _replicas;

        if ( _to_commit == 0 )
        {
            result  = true;
            timeout = false;

            notify();
        }

        return __replicas;
    }

    /* ---------------------------------------------------------------------- */
    /* Class access methods                                                   */
    /* ---------------------------------------------------------------------- */
    int index()
    {
        return _index;
    }

    int replicas()
    {
        return _replicas;
    }

    int to_commit()
    {
        return _to_commit;
    }

    void to_commit(int c)
    {
        _to_commit = c;
    }

private:
    /**
     *  Index for this log entry
     */
    unsigned int _index;

    /**
     *  Remaining number of servers that need to replicate this record to commit
     *  it. Initialized to ( Number_Servers - 1 ) / 2
     */
    int _to_commit;

    /**
     *  Total number of replicas for this entry
     */
    int _replicas;
};

/**
 * This class represents a map of replication requests. It syncs access between
 * RaftManager and DB writer threads. A DB writer allocates and set the 
 * request and then it waits on it for completion.
 */
class ReplicaRequestMap
{
public:
    ReplicaRequestMap()
    {
        pthread_mutex_init(&mutex, 0);
    };

    virtual ~ReplicaRequestMap()
    {
        pthread_mutex_destroy(&mutex);
    }

    /**
     *  Increments the number of replicas of this request. If it can be
     *  committed the request is removed from the map
     *    @param rindex of the request
     *
     *    @return the number of replicas to commit, if 0 it can be committed
     */
    int add_replica(int rindex)
    {
        int to_commit = -1;

        pthread_mutex_lock(&mutex);

        std::map<int, ReplicaRequest *>::iterator it = requests.find(rindex);

        if ( it != requests.end() && it->second != 0 )
        {
            it->second->add_replica();

            to_commit = it->second->to_commit();

            if ( to_commit == 0 )
            {
                requests.erase(it);
            }
        }

        pthread_mutex_unlock(&mutex);

        return to_commit;
    }

    /**
     *  Allocated an empty replica request. It marks a writer thread will wait
     *  on this request.
     *    @param rindex of the request
     */
    void allocate(int rindex)
    {
        pthread_mutex_lock(&mutex);

        std::map<int, ReplicaRequest *>::iterator it = requests.find(rindex);

        if ( it == requests.end() )
        {
            requests.insert(std::make_pair(rindex, (ReplicaRequest*) 0));
        }

        pthread_mutex_unlock(&mutex);
    }

    /**
     * Set the replication request associated to this index. If there is no
     * previous request associated to the index it is created.
     *   @param rindex of the request
     *   @param rr replica request pointer
     */
    void set(int rindex, ReplicaRequest * rr)
    {
        pthread_mutex_lock(&mutex);

        std::map<int, ReplicaRequest *>::iterator it = requests.find(rindex);

        if ( it == requests.end() )
        {
            requests.insert(std::make_pair(rindex, rr));
        }
        else if ( it->second == 0 )
        {
            it->second = rr;
        }

        pthread_mutex_unlock(&mutex);
    }

    /**
     *  Notify all writers and clear the replica map
     */
    void clear()
    {
        pthread_mutex_lock(&mutex);

        std::map<int, ReplicaRequest *>::iterator it;

        for ( it = requests.begin() ; it != requests.end() ; ++it )
        {
            if ( it->second == 0 )
            {
                continue;
            }

            it->second->result  = false;
            it->second->timeout = false;
            it->second->message = "oned is now follower";

            it->second->notify();
        }

        requests.clear();

        pthread_mutex_unlock(&mutex);
    }

    /**
     *  @return true if a replica request needs to be replicated
     *    - last DB index is greater than current replicated index
     *    - there is no allocation in progress for the next rindex 
     */
    bool need_replica(int db_index, int rindex)
    {
        if ( db_index <= rindex )
        {
            return false;
        }

        pthread_mutex_lock(&mutex);

        std::map<int,ReplicaRequest *>::iterator it = requests.find(rindex + 1);

        bool rc = true;

        if ( it != requests.end() && it->second == 0 )
        {
            rc = false;
        }

        pthread_mutex_unlock(&mutex);

        return rc;
    }

private:
    pthread_mutex_t mutex;

    /**
     * Clients waiting for a log replication
     */
    std::map<int, ReplicaRequest *> requests;
};

#endif /*REPLICA_REQUEST_H_*/

