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
};

class case_mapping {
public:
	int first;
	int last;
	mutable int data_size;
	mutable int branch_count;

	case_mapping(int first, int last) : first(first), last(last), data_size(0), branch_count(0) {}
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
		unsigned i;
		bool is_short = true;
		char c = '{';
		for (i = 0; i < cm.size(); i++) {
			if (cm[i] > 0xffff) {
				is_short = false;
				break;
			}
		}
		fprintf(out, "\tstatic const unsigned%s ucase_%04X_%04X[] = ", is_short ? " short" : "", first, last);
		for (i = 0; i < cm.size(); i++) {
			fprintf(out, "%c0x%04X", c, cm[i]);
			c = ',';
		}
		fprintf(out, "};\n");
		fprintf(out, "\treturn ucase_%04X_%04X[%s - 0x%04X];\n", first, last, var, first);
		data_size = cm.size() * (is_short ? 2 : 4);
		branch_count = 0;
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

class exclusion_mapping : public case_mapping {
	casemap ex;
protected:
	virtual const char *expr(const char *var) const = 0;
public:
	exclusion_mapping(int first, int last, const casemap &e) : case_mapping(first, last), ex(e) {}
	void print_ret(FILE *out, const char *var) const {
		if (ex.size() == 1) {
			fprintf(out, "\treturn %s != 0x%04X ? %s : 0x%04X;\n",
					var, ex.begin()->first, expr(var), ex.begin()->second);
			branch_count = 1;
		} else {
			charmap c;
			branch_count = 0;
			for (casemap::const_iterator i = ex.begin(); i != ex.end(); ++i)
				c.push_back(i->second);
			xlat_mapping x(ex.begin()->first, ex.rbegin()->first, c);
			if (ex.begin()->first > first) {
				fprintf(out, "\tif (%s < 0x%04X) return %s;\n",
						var, ex.begin()->first, expr(var));
				branch_count++;
			}
			if (ex.rbegin()->first < last) {
				fprintf(out, "\tif (%s > 0x%04X) return %s;\n", var, ex.rbegin()->first, expr(var));
				branch_count++;
				x.print_ret(out, var);
			} else {
				x.print_ret(out, var);
			}
			data_size = x.data_size;
		}
	}
};

class delta_ex_mapping : public exclusion_mapping {
	int delta;
protected:
	const char *expr(const char *var) const {
		static char buf[64];
		snprintf(buf, sizeof(buf), "%s %c %d", var, delta < 0 ? '-' : '+', delta < 0 ? -delta : delta);
		return buf;
	}
public:
	delta_ex_mapping(int first, int last, const casemap &e, int delta) : exclusion_mapping(first, last, e), delta(delta) {}
};

class set_mapping : public case_mapping {
public:
	set_mapping(int first, int last) : case_mapping(first, last) {}
	void print_ret(FILE *out, const char *var) const {
		fprintf(out, "\treturn %s | 1;\n", var);
	}
};

class set_ex_mapping : public exclusion_mapping {
protected:
	const char *expr(const char *var) const {
		static char buf[64];
		snprintf(buf, sizeof(buf), "%s | 1", var);
		return buf;
	}
public:
	set_ex_mapping(int first, int last, const casemap &e) : exclusion_mapping(first, last, e) {}
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
make_sequental(casemap &m, int (*cm)(int c, int arg), int arg)
{
	int brk = 0;
	if (m.empty())
		return false;
	do {
		int last;
		brk = 0;
		casemap::const_iterator i = m.begin();
		last = i->first;
		while (++i != m.end()) {
			if (i->first != last + 1)
				brk++;
			last++;
		}
		if (brk > 4)
			return false;
		i = m.begin();
		last = i->first;
		while (++i != m.end()) {
			if (i->first != last + 1) {
				m[last + 1] = cm(last + 1, arg);
				break;
			}
			last++;
		}
	} while (brk);
	return true;
}

static int map_set(int c, int dummy)
{
	return c | 1;
}

static int map_delta(int c, int delta)
{
	return c + delta;
}

case_mapping *get_mapping(int first, int last, const charmap &m)
{
	int delta;
	int i;
	int setl = 0, resl = 0, dell = 0;
	casemap set_ex, delta_ex;
	charset setc, resc;
	if (m.size() == 1)
		return new single_mapping(first, m[0]);
	delta = m[0] - first;
	for (i = 0; i < (int)m.size(); i++) {
		if (first + i + delta != m[i]) {
			delta_ex[first + i] = m[i];
		} else {
			dell++;
		}
/*		if (first == 0x1c4) {
			fprintf(stderr, "%04X => %04X %s\n", first + i, m[i],
					((first + i) | 1) != m[i] ? "!!!" : "");
		}*/
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
	if (setl && setl != (int)m.size())
		fprintf(stderr, "set: %04X: %d of %d\n", first, setl, (int)m.size());
	if (resl && resl != (int)m.size())
		fprintf(stderr, "res: %04X: %d of %d\n", first, m.size() - resl, (int)m.size());
	if (dell && dell != (int)m.size())
		fprintf(stderr, "del: %04X: %d of %d\n", first, dell, (int)m.size());

#if 1
	if (delta_ex.empty())
		return new delta_mapping(first, last, delta);
	else if (dell + delta_ex.size() == m.size() && delta_ex.size() == 1 && dell > 4)
		return new delta_ex_mapping(first, last, delta_ex, delta);
	else if (setl == m.size())
		return new set_mapping(first, last);
	else if (!resl)
		return new reset_mapping(first, last);
	else if (setl + set_ex.size() == m.size() && make_sequental(set_ex, map_set, 0))
		return new set_ex_mapping(first, last, set_ex);
	else
#endif
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
		int branches = 0;
		int data = 0;
		std::vector<case_mapping*> m;
		m.resize(1 << (m_tree->height + 1));
		fprintf(out, "/* tree height is %d */\n", m_tree->height + 1);
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
				branches += m[i]->branch_count + 1;
				data += m[i]->data_size;
//				if (m[i]->cm.size() != m[i]->last - m[i]->first + 1)
//					abort();
			}
		}
		fprintf(out, "/* %d branches, %d cdata bytes */\n", branches, data);
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
		if (i->first - last > 12) {
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
	if (!m.empty()) {
		cm_data d(first, last, m);
		mi.insert(d);
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
