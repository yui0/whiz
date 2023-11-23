//---------------------------------------------------------
//	Debug for Whiz (Japanese Input Method Engine)
//
//		(C)2003 NAKADA
//---------------------------------------------------------

#ifdef DEBUG
	#define debug( whizdebug )	 whizdebug
#else
	#define debug( whizdebug )
#endif

#define D_ERR	0	// ���顼
#define D_WARN	1	// �ٹ�
#define D_INFO	5	// ����ե��᡼�����

#ifdef DEBUG
	void dmsg(int lev, const char *f, ...);
#endif

void smsg(int lev, const char *f, ...);
