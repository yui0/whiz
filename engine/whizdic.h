//---------------------------------------------------------
//	Whiz Dic (Japanese Input Method Engine)
//
//		(C)2003,2007 NAKADA
//---------------------------------------------------------

//#include "whiz.h"


//---------------------------------------------------------
//	Structure
//---------------------------------------------------------

// ñ�켭��
struct DIC {
	short cost;		// ������ (0-4000)
	short wclass;		// �ʻ� (0-91)
	short type;		// ���ѷ�
	short form;		// ���ѷ�

	//char *read;
	//char *word;
	char read[WORDMAX];	// �ɤ�
	char word[WORDMAX];	// ñ��
	char org[WORDMAX];	// ����

	char len;		// �ɤߤ�Ĺ�� (WORDMAX=100)
	int num;		// ��Ͽ�ֹ�
	char freq;		// �������� (ñ��ؽ�)
};

// ñ�켭�����
class DIC_INFO {
public:
	int num;		// ���(number)
	int f[LINEMAX];		// �⤦����������
	int c[LINEMAX];		// ������

	// �����
	void init()
	{
		int i;
		for (i=LINEMAX-1; i>=0; i--) f[i]=-1;
		num=0;
	}
};

// Ϣ�ܼ���
struct CONNECT {
	int cost;		// ������
	int wclass[2];		// �ʻ�
	int type[2];		// ���ѷ�
	int form[2];		// ���ѷ�
	char word[2][WORDMAX];	// ���ò��ʻ����
};

// Ϣ�ܾ���
struct CONNECT_INFO {
	int wclass[90][90];
};

// ���Ѽ���
struct INFLECT {
	// ���ѷ�
	char type[30];

	int form[FORMSMAX];		// ���ѷ�
	char word[FORMSMAX][30];	// ����
	char read[FORMSMAX][30];	// �ɤ�
	char pro[FORMSMAX][30];		// ȯ��
};

// �ѥ�
struct PATH {
	DIC *d;			// Pointer of Dictionary

	int b;			// Back Path
	int n;			// Next Path

	int nc;			// Next Choise

	int cost;		// Cost
	int tc;			// Total Cost

	int len;
	int flag;
};

// �ѥ�����
class PATH_INFO {
public:
	int num;		// Number of path

	int sn;			// Number of sentence
	PATH *s[MAXSENTENCE];	// Sentence

	//int cost[WORDMAX/2];

	void init()
	{
		int i;
		num=sn=0;
		//for (i=0; i<WORDMAX/2; i++) cost[i]=0;
		for (i=MAXSENTENCE-1; i>=0; i--) s[i]=0;
	}
	int adds(PATH *p)
	{
		// add sentence
		//s[sn++]=p;
		//if (sn>=50) { printf("Error: sentence overflow !!"); sn=0; return 1; }

		int i;
		for (i=0; i<sn; i++) {
			if (s[i]->tc > p->tc) {
				PATH *a;
				a=s[i];
				s[i]=p;
				p=a;
			}
		}
		s[sn++]=p;

		if (sn>=MAXSENTENCE) { printf("Error: sentence overflow !!"); sn=MAXSENTENCE-1; return 1; }

		return 0;
	}
	void sentence();
};
