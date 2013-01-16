#include <map>
#include <vector>
#include <stdio.h>
#include "avl.h"
#include <stdlib.h>
#include <set>
#include <string.h>
#include <getopt.h>
#include <stdarg.h>
#include <errno.h>

typedef std::map<int, int> casemap;
typedef std::vector<int> charmap;
typedef std::set<int> charset;

typedef void (*gen_res_cb)(FILE *out, const char *fmt, ...);

static void
gen_ret_cb(FILE *out, const char *fmt, ...)
{
	va_list ap;
	fprintf(out, "\treturn ");
	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
	fprintf(out, ";\n");
}

static void
gen_var_cb(FILE *out, const char *fmt, ...)
{
	va_list ap;
	fprintf(out, "\t{ oc = ");
	va_start(ap, fmt);
	vfprintf(out, fmt, ap);
	va_end(ap);
	fprintf(out, "; break; }\n");
}

static bool
	allow_delta = true,
	allow_delta_ex = true,
	allow_set = true,
	allow_set_ex = true,
	allow_res = true;

static int span = 12, spanu8 = 0;

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

	virtual void print_ret(FILE *out, const char *var, gen_res_cb res) const = 0;
};

class xlat_mapping : public case_mapping {
	charmap		cm;
public:
	xlat_mapping(int first, int last, const charmap &cm) : case_mapping(first, last), cm(cm) {}

	void print_ret(FILE *out, const char *var, gen_res_cb res) const {
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
		res(out, "ucase_%04X_%04X[%s - 0x%04X]", first, last, var, first);
		data_size = cm.size() * (is_short ? 2 : 4);
		branch_count = 0;
	}
};

class delta_mapping : public case_mapping {
	int delta;
public:
	delta_mapping(int first, int last, int delta) : case_mapping(first, last), delta(delta) {}

	void print_ret(FILE *out, const char *var, gen_res_cb res) const {
		if (delta > 0)
			res(out, "%s + %d", var, delta);
		else
			res(out, "%s - %d", var, -delta);
	}
};

class exclusion_mapping : public case_mapping {
	casemap ex;
protected:
	virtual const char *expr(const char *var) const = 0;
public:
	exclusion_mapping(int first, int last, const casemap &e) : case_mapping(first, last), ex(e) {}
	void print_ret(FILE *out, const char *var, gen_res_cb res) const {
		if (ex.size() == 1) {
			res(out, "%s != 0x%04X ? %s : 0x%04X",
					var, ex.begin()->first, expr(var), ex.begin()->second);
			branch_count = 1;
		} else {
			charmap c;
			branch_count = 0;
			for (casemap::const_iterator i = ex.begin(); i != ex.end(); ++i)
				c.push_back(i->second);
			xlat_mapping x(ex.begin()->first, ex.rbegin()->first, c);
			if (ex.begin()->first > first) {
				fprintf(out, "\tif (%s < 0x%04X)\n\t",
						var, ex.begin()->first);
				res(out, "%s", expr(var));
				branch_count++;
			}
			if (ex.rbegin()->first < last) {
				fprintf(out, "\tif (%s > 0x%04X)\n\t",
					var, ex.rbegin()->first);
				res(out, "%s", expr(var));
				branch_count++;
				x.print_ret(out, var, res);
			} else {
				x.print_ret(out, var, res);
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
	void print_ret(FILE *out, const char *var, gen_res_cb res) const {
		res(out, "%s | 1", var);
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
	void print_ret(FILE *out, const char *var, gen_res_cb res) const {
		res(out, "%s !!! 1", var);
	}
};

class single_mapping : public case_mapping {
	int result;
public:
	single_mapping(int in, int out) : case_mapping(in, in), result(out) {}
	void print_ret(FILE *out, const char *var, gen_res_cb res) const {
		res(out, "0x%04X", result);
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

	if (allow_delta && delta_ex.empty())
		return new delta_mapping(first, last, delta);
	else if (allow_delta_ex && (dell + delta_ex.size() == m.size() && delta_ex.size() == 1 && dell > 4))
		return new delta_ex_mapping(first, last, delta_ex, delta);
	else if (allow_set && setl == (int)m.size())
		return new set_mapping(first, last);
	else if (allow_res && !resl)
		return new reset_mapping(first, last);
	else if (allow_set_ex && (setl + set_ex.size() == m.size() && make_sequental(set_ex, map_set, 0)))
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
	static int free_node(void *n) {
		delete (case_mapping*)n;
		return 1;
	}
public:
	map_info() {
		m_tree = avl_tree_new(avl_cmp, this);
	}
	~map_info() {
		avl_tree_free(m_tree, free_node);
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

	void dump(FILE *out, const char *var, gen_res_cb res) {
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
				fprintf(out, "\tif (%s < 0x%04X)\n\t", var, m[i]->first);
//				fprintf(out, "\tif (%s > 0x%04X)\n", var, m[i]->last);
				j = 2 * i + 1;
				if (j < m.size() && m[j])
					fprintf(out, "\tgoto %s;\n", m[j]->label());
				else
					res(out, "%s", var);
				fprintf(out, "\tif (%s > 0x%04X)\n\t", var, m[i]->last);
//				fprintf(out, "\tif (%s < 0x%04X)\n", var, m[i]->first);
				j = 2 * i + 2;
				if (j < m.size() && m[j])
					fprintf(out, "\tgoto %s;\n", m[j]->label());
				else
					res(out, "%s", var);
				m[i]->print_ret(out, var, res);
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

static void
codegen(casemap::const_iterator begin, casemap::const_iterator end, FILE *out, const char *var, gen_res_cb res, unsigned sp)
{
	charmap m;
	unsigned cvt = 0;
	int last = 0, cnt = 0, first = 0;
	map_info mi;
	while (begin != end && begin->first == begin->second)
		++begin;
	while (end != begin && end->first == end->second)
		--end;
	if (begin == end)
		return;
	first = begin->first;
	for (casemap::const_iterator i = begin; i != end; ++i) {
		cvt++;
		if (i->first - last > sp) {
			if (!m.empty()) {
				cm_data d(first, last, m);
				mi.insert(d);
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
	mi.dump(out, var, res);
	fprintf(out, "//%d case conversions\n", cvt);
}

static void
gen_u_cvt(const casemap &cm, const char *fname)
{
	FILE *out = fopen(fname, "w");
	if (!out) {
		perror("fopen");
		exit(EXIT_FAILURE);
	}
	codegen(cm.begin(), cm.end(), out, "c", gen_ret_cb, span);
	fclose(out);
}

static void
gen_u8_cvt(const casemap &cm, const char *ftmpl)
{
	static const struct {
		unsigned	first, last;
	} u8r[] = {
		{0, 0x7f},
		{0x80, 0x7ff},
		{0x800, 0xffff},
		{0x10000, 0x1fffff}
	};
	for (unsigned i = 0; i < sizeof(u8r) / sizeof(*u8r); i++) {
		casemap::const_iterator b, e;
		char fname[1024];
		snprintf(fname, sizeof(fname), "%s_%04X_%04X.h", ftmpl, u8r[i].first, u8r[i].last);
		FILE *out = fopen(fname, "w");
		if (!out) {
			perror("fopen");
			exit(EXIT_FAILURE);
		}
		fprintf(out, "do {\n");
		b = cm.lower_bound(u8r[i].first);
		if (b != cm.end()) {
			e = cm.upper_bound(u8r[i].last);
			codegen(b, e, out, "ic", gen_var_cb, spanu8);
		}
		fprintf(out, "} while (0);\n");
		fclose(out);
	}
}

static casemap cm;

int main(int argc, char **argv)
{
	int c;
	FILE *in;
	char line[4096];

	while (1) {
		c = getopt(argc, argv, "l:L:dDsSxXyY");
		if (c == -1)
			break;
		switch (c) {
		case 'l':
			span = atoi(optarg);
			break;
		case 'L':
			spanu8 = atoi(optarg);
			break;
		case 'd':
			allow_delta = true; break;
		case 'D':
			allow_delta = false; break;
		case 's':
			allow_set = true; break;
		case 'S':
			allow_set = false; break;
		case 'x':
			allow_delta_ex = true; break;
		case 'X':
			allow_delta_ex = false; break;
		case 'y':
			allow_set_ex = true; break;
		case 'Y':
			allow_set_ex = false; break;
		default:
			fprintf(stderr, "Invalid option: %c\n", c);
			exit(EXIT_FAILURE);
		}
	}

	in = fopen("CaseFolding.txt", "r");
	if (!in) {
		fprintf(stderr, "Can't locate CaseFolding.txt file. Exiting.\n");
		exit(EXIT_FAILURE);
	}
	while ((fgets(line, sizeof(line), in))) {
		int c1, c2;
		char type;
		if (sscanf(line, "%x; %c; %x", &c1, &type, &c2) == 3 && (type == 'S' || type == 'C')) {
			cm[c1] = c2;
		}
	}
	fclose(in);
	if (span)
		gen_u_cvt(cm, "/tmp/x");
	if (spanu8)
		gen_u8_cvt(cm, "/tmp/u");
	return 0;
}
