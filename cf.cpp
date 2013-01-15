#include <map>
#include <vector>
#include <stdio.h>
#include "avl.h"
#include <stdlib.h>
#include <set>
#include <string.h>

typedef std::map<int, int> casemap;
typedef std::vector<int> charmap;
typedef std::set<int> charset;

struct cm_data {
	int			first;
	int			last;
	charmap		cm;

	cm_data() {}
	cm_data(int first, int last, const charmap &cm) : first(first), last(last), cm(cm) {}
	const char *label() const {
		static char buf[32];
		sprintf(buf, "range_%04X_%04X", first, last);
		return buf;
	}

	void print_ret(FILE *out, const char *var) const {
		if (cm.size() > 1) {
			char c = '{';
			fprintf(out, "\tstatic const unsigned short ucase_%04X_%04X[] = ", first, last);
			for (unsigned i = 0; i < cm.size(); i++) {
				fprintf(out, "%c0x%04X", c, cm[i]);
				c = ',';
			}
			fprintf(out, "};\n");
			fprintf(out, "\treturn ucase_%04X_%04X[%s - 0x%04X];\n", first, last, var, first);
		} else {
			fprintf(out, "\treturn 0x%04X;\n", cm[0]);
		}
	}
};

class case_mapping {
public:
	int first;
	int last;

	case_mapping(int first, int last) : first(first), last(last) {}
	virtual ~case_mapping() {};

	bool contains(const case_mapping &m) const {
		return first <= m.first && last >= m.last;
	}

	const char *label() const {
		static char buf[32];
		sprintf(buf, "range_%04X_%04X", first, last);
		return buf;
	}

	virtual void print_ret(FILE *out, const char *var) const = 0;
};

class xlat_mapping : public case_mapping {
	charmap		cm;
public:
	xlat_mapping(int first, int last, const charmap &cm) : case_mapping(first, last), cm(cm) {}

	void print_ret(FILE *out, const char *var) const {
		char c = '{';
		fprintf(out, "\tstatic const unsigned short ucase_%04X_%04X[] = ", first, last);
		for (unsigned i = 0; i < cm.size(); i++) {
			fprintf(out, "%c0x%04X", c, cm[i]);
			c = ',';
		}
		fprintf(out, "};\n");
		fprintf(out, "\treturn ucase_%04X_%04X[%s - 0x%04X];\n", first, last, var, first);
	}
};

class delta_mapping : public case_mapping {
	int delta;
public:
	delta_mapping(int first, int last, int delta) : case_mapping(first, last), delta(delta) {}

	void print_ret(FILE *out, const char *var) const {
		if (delta > 0)
			fprintf(out, "\treturn %s + %d;\n", var, delta);
		else
			fprintf(out, "\treturn %s - %d;\n", var, -delta);
	}
};

class set_mapping : public case_mapping {
public:
	set_mapping(int first, int last) : case_mapping(first, last) {}
	void print_ret(FILE *out, const char *var) const {
		fprintf(out, "\treturn %s | 1;\n", var);
	}
};

class set_ex_mapping : public case_mapping {
	casemap ex;
public:
	set_ex_mapping(int first, int last, const casemap &e) : case_mapping(first, last), ex(e) {}
	void print_ret(FILE *out, const char *var) const {
		if (ex.size() == 1)
			fprintf(out, "\treturn %s != 0x%04X ? %s | 1 : 0x%04X;\n",
					var, ex.begin()->first, var, ex.begin()->second);
		else {
			charmap c;
			for (casemap::const_iterator i = ex.begin(); i != ex.end(); ++i)
				c.push_back(i->second);
			xlat_mapping x(ex.begin()->first, ex.rbegin()->first, c);
			if (ex.begin()->first > first)
				fprintf(out, "\tif (%s < 0x%04X) return %s | 1;\n",
						var, ex.begin()->first, var);
			if (ex.rbegin()->first < last) {
				fprintf(out, "\tif (%s > 0x%04X) return %s | 1;\n", var, ex.rbegin()->first, var);
				x.print_ret(out, var);
			} else {
				x.print_ret(out, var);
			}
		}
	}
};

class reset_mapping : public case_mapping {
public:
	reset_mapping(int first, int last) : case_mapping(first, last) {}
	void print_ret(FILE *out, const char *var) const {
		fprintf(out, "\treturn %s !!! 1;\n", var);
	}
};

class single_mapping : public case_mapping {
	int result;
public:
	single_mapping(int in, int out) : case_mapping(in, in), result(out) {}
	void print_ret(FILE *out, const char *var) const {
		fprintf(out, "\treturn 0x%04X;\n", result);
	}
};

static bool
make_sequental(casemap &m, int (*cm)(int c))
{
	int last, brk = 0;
	if (m.empty())
		return false;
	casemap::const_iterator i = m.begin();
	last = i->first;
	while (++i != m.end()) {
		if (i->first != last + 1)
			brk++;
		last++;
	}
	if (brk > 2)
		return false;
	while (brk) {
		i = m.begin();
		last = i->first;
		while (++i != m.end()) {
			if (i->first != last + 1) {
				m[last + 1] = cm(last + 1);
				brk--;
				break;
			}
			last++;
		}
	}
	return true;
}

static int map_set(int c)
{
	return c | 1;
}

case_mapping *get_mapping(int first, int last, const charmap &m)
{
	int delta;
	int i;
	bool dm = true;
	int setl = 0, resl = 0;
	casemap set_ex;
	charset setc, resc;
	if (m.size() == 1)
		return new single_mapping(first, m[0]);
	delta = m[0] - first;
	for (i = 0; i < (int)m.size(); i++) {
		if (first + i + delta != m[i])
			dm = false;
		if (first == 0x1c4) {
			fprintf(stderr, "%04X => %04X %s\n", first + i, m[i],
					((first + i) | 1) != m[i] ? "!!!" : "");
		}
		if (((first + i) | 1) == m[i]) {
			setl++;
			setc.insert(first + i);
		} else {
			set_ex[first + i] = m[i];
		}
		if ((m[i] & ~1) != first + i) {
			resl++;
		} else {
			resc.insert(first + i);
		}
	}
	if (setl && setl != m.size())
		fprintf(stderr, "set: %04X: %d of %d\n", first, setl, (int)m.size());
	if (resl && resl != m.size())
		fprintf(stderr, "res: %04X: %d of %d\n", first, m.size() - resl, (int)m.size());

	if (dm)
		return new delta_mapping(first, last, delta);
	else if (setl == m.size())
		return new set_mapping(first, last);
	else if (!resl)
		return new reset_mapping(first, last);
	else if (setl + set_ex.size() == m.size() && make_sequental(set_ex, map_set))
		return new set_ex_mapping(first, last, set_ex);
	else
		return new xlat_mapping(first, last, m);
}

class map_info {
	avl_tree				*m_tree;
	std::vector<cm_data*>	m_prio;
	static int avl_cmp(void *arg, void *k1, void *k2) {
		case_mapping *l = (case_mapping*)k1, *r = (case_mapping*)k2;
		return l->first < r->first ? -1 : l->first > r->first ? 1 : 0;
	}
	static void dump_helper(case_mapping **a, int i, avl_node *cur) {
		if (cur) {
//			printf("%d: %d\n", i, ((cm_data*)cur->key)->first);
			a[i] = (case_mapping*)cur->key;
			dump_helper(a, 2 * i + 1, cur->left);
			dump_helper(a, 2 * i + 2, cur->right);
		}
	}
public:
	map_info() {
		m_tree = avl_tree_new(avl_cmp, this);
	}

	void insert(const cm_data &d) {
		case_mapping *p;
#if 0
		/* Check for priority intervals */
		if ((d.first <= 'T' && d.last >= 'T') ||
				(d.first <= 0x0410 && d.last >= 0x0410))
		{
			/* Interval contains ASCII or Cyrillic letters */
			m_prio.push_back(new cm_data(d));
		}
#endif
		if (avl_get_by_key(m_tree, (void*)&d, (void**)&p) != 0) {
			p = get_mapping(d.first, d.last, d.cm);
			avl_insert(m_tree, p);
		}
	}

	void dump(FILE *out) {
		std::vector<case_mapping*> m;
		m.resize(1 << (m_tree->height + 1));
		dump_helper(&m[0], 0, m_tree->root->right);
		for (unsigned i = 0; i < m.size(); i++) {
			if (m[i]) {
				unsigned j;
#if 1
				if (i)
					fprintf(out, "%s:\n", m[i]->label());
				fprintf(out, "\tif (c < 0x%04X)\n", m[i]->first);
//				fprintf(out, "\tif (c > 0x%04X)\n", m[i]->last);
				j = 2 * i + 1;
				if (j < m.size() && m[j])
					fprintf(out, "\t\tgoto %s;\n", m[j]->label());
				else
					fprintf(out, "\t\treturn c;\n");
				fprintf(out, "\tif (c > 0x%04X)\n", m[i]->last);
//				fprintf(out, "\tif (c < 0x%04X)\n", m[i]->first);
				j = 2 * i + 2;
				if (j < m.size() && m[j])
					fprintf(out, "\t\tgoto %s;\n", m[j]->label());
				else
					fprintf(out, "\t\treturn c;\n");
				m[i]->print_ret(out, "c");
#else
				/* This is "test" generator that makes sequences of "char in [first, last]" statements */
				fprintf(out, "if (c >= 0x%04X && c <= 0x%04X) {\n", m[i]->first, m[i]->last);
				m[i]->print_ret(out, "c");
				fprintf(out, "}\n");
#endif
//				if (m[i]->cm.size() != m[i]->last - m[i]->first + 1)
//					abort();
			}
		}
	}
};

static casemap cm;

int main()
{
	int last = 0, cnt = 0, first = 0;
	charmap m;
	FILE *in;
	char line[4096];
	in = fopen("CaseFolding.txt", "r");
	while ((fgets(line, sizeof(line), in))) {
		int c1, c2;
		char type;
		if (sscanf(line, "%x; %c; %x", &c1, &type, &c2) == 3 && (type == 'S' || type == 'C')) {
			cm[c1] = c2;
		}
	}
	fclose(in);
	first = cm.begin()->first;
	map_info mi;
	for (casemap::const_iterator i = cm.begin(), end = cm.end(); i != end; ++i) {
		if (i->first - last > 4) {
			if (!m.empty()) {
				cm_data d(first, last, m);
				mi.insert(d);
//				if (i != cm.begin())
//					printf("%04X - %04X: %d\n", first, last, (int)m.size());
				m.clear();
			}
			first = i->first;
			cnt++;
		} else {
			while (last + 1 < i->first)
				m.push_back(++last);
		}
		m.push_back(i->second);
		last = i->first;
	}
	mi.dump(stdout);
	in = fopen("/tmp/ucd", "w");
	for (int i = 0; i < 0x10000; i++) {
		casemap::const_iterator k = cm.find(i);
//		if (k != cm.end())
//			fprintf(in, "%04X => %04X\n", i, k->second);
		fprintf(in, "%04X => %04X\n", i, k == cm.end() ? i : k->second);
	}
	fclose(in);
	printf("//%d case conversions\n", cm.size());
	return 0;
}
