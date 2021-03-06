#include "logic_server.h"
#include "base/ef_utility.h"

namespace gim{

static int deleteDispatcher(void* obj){
	delete (Dispatcher*)obj;
	return 0;
}

int LogicServer::initDispatcher(){	
	
	for(int i = 0; i< m_thread_cnt; ++i){

		EventLoop& l = m_cliset.getEventLoop(i);
		Dispatcher* pDBC = new Dispatcher(&l);

		if(pDBC->init(m_shf) < 0){
			delete pDBC;
			return -1;
		}
		l.setObj(pDBC, deleteDispatcher);
	}


	return  0;

}

LogicServer::LogicServer()
	:m_thread_cnt(0),
	m_keepalive_span(6000),
	m_reconnect_span(0),
	m_service_type(0),
	m_confac(NULL){
}


LogicServer::~LogicServer(){
	freeServerListCache();
}


int LogicServer::startListen(int port, LogicConFactory* cfac, 
		ConnectionDispatcher* d){
	cfac->setLogicServer(this);
	return m_cliset.startListen(port, cfac, d);
}

int LogicServer::stopListen(int port){
	return m_cliset.stopListen(port);
}

int LogicServer::initServerListCache(const Json::Value& v){
	m_svchfct = SvLstChFactory::create(v);
	m_cache = m_svchfct->newSvLstCache();

	if(!m_cache){
		return -1;
	}

	m_cache->setServerListListener(this);
	m_cache->watchServerList(0);

	return 0;
}

int LogicServer::initSessCacheFactory(const Json::Value& v){
	m_shf = SsChFactory::create(v);
	return 0;
}

void LogicServer::freeSessCacheFactory(){
	if(m_shf){
		delete m_shf;
		m_shf = NULL;
	}
}

void LogicServer::freeServerListCache(){
	if(m_cache){
		delete m_cache;
		m_cache = NULL;
	}

	if(m_svchfct){
		delete m_svchfct;
		m_svchfct = NULL;
	}
}

void serverListAdds(const vector<Serv>& a, const vector<Serv>& b,
	vector<Serv>& adds){
	for(size_t i = 0; i < b.size(); ++i){
		size_t j = 0;
		for(; j < a.size(); ++j){
			if(b[i].id == a[j].id)
				break;
		}
		if(j >= a.size()){
			adds.push_back(b[i]);
		}
	}
}

void serverListDiff(const vector<Serv>& a, const vector<Serv>& b, 
	vector<Serv>& adds, vector<Serv>& dels){
	serverListAdds(a, b, adds);
	serverListAdds(b, a, dels);
}

void serverListDels(vector<Serv>& a, const Serv& b){
	for(vector<Serv>::iterator i = a.begin(); i < a.end(); ++i){
		if(i->id == b.id){
			a.erase(i);
			break;
		}
	}
}

//if has new connect server, connect it
int LogicServer::onListChange(int type, vector<Serv> &slist){

	vector<Serv> adds;
	vector<Serv> dels;
	
	serverListDiff(m_servlist, slist, adds, dels);

	for(size_t i = 0; i < adds.size(); ++i){
		allThreadConnectServer(slist[i]);
		m_servlist.push_back(adds[i]);
	}		

	for(size_t j = 0; j < dels.size(); ++j){
		serverListDels(m_servlist, dels[j]);	
	}

	return 0;
}

int LogicServer::connectIPArray(SvCon* c, const Json::Value& a, 
	int port){

	int ret = 0;
	string addr;

	if(a.size() <= 0){
		return -1;
	}

	for(Json::UInt i = 0; i < a.size(); ++i){
		addr = a[i].asString();
		ret = c->connectTo(addr, port);
		
		if(ret >= 0){
			c->setAddr(addr, port);
			break;	
		}
	}

	return ret;

}

int LogicServer::allThreadConnectServer(const Serv& s){

	for(int i = 0; i < m_thread_cnt; ++i){
		connectServer(s, m_id * 100000 + m_thread_cnt % 100000);
	}
	
	return 0;
}

int LogicServer::connectServer(const Serv& s, int svid){
	int ret = 0;
	SvCon* c = m_confac->createSvCon(NULL);

	c->setConnectServerId(s.id);
	c->setServerId(svid);
	c->setServiceType(m_service_type);
	c->setReconnectSpan(m_reconnect_span);
	c->setKeepAliveSpan(m_keepalive_span);
	c->setLogicServer(this);
	
	int port = s.v[GIM_SERVER_LISTEN_PORT].asInt();

	//try localips
	ret = connectIPArray(c, s.v[GIM_LOCAL_IPS], port);
	
	//try publicips
	if(ret < 0){
		ret = connectIPArray(c, s.v[GIM_PUBLIC_IPS], port);
	}

	static volatile int id = 0;
	int conid = atomicIncrement32(&id);
	EventLoop& l = m_cliset.getEventLoop(conid % 
		m_cliset.getEventLoopCount());
	c->setId(conid);
	l.asynAddConnection(conid, c);	
	return 0;
}

int LogicServer::run(){
	m_cliset.run();
	return 0;	
}

int LogicServer::stop(){
	m_cliset.stop();
	return 0;
}

int LogicServer::init(int threadcnt, const Json::Value& svlstconf,
	const Json::Value& sschconf){

	int ret = 0;
	m_thread_cnt = threadcnt;

	m_cliset.setEventLoopCount(threadcnt);
	m_cliset.init();

	ret = initServerListCache(svlstconf);
	
	if(ret < 0)
		return ret;	

	initSessCacheFactory(sschconf);
	
	ret = initDispatcher();

	return ret;
}


};
