#include "svghandler.h"

#include "log.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QXmlStreamReader>
#include <QStack>
#include <QtMath>

#define logSvgI() nCInfo("svg")
#define logSvgD() nCDebug("svg")

// '0' is 0x30 and '9' is 0x39
static inline bool isDigit(ushort ch)
{
	static quint16 magic = 0x3ff;
	return ((ch >> 4) == 3) && (magic >> (ch & 15));
}

static qreal toDouble(const QChar *&str)
{
	const int maxLen = 255;//technically doubles can go til 308+ but whatever
	char temp[maxLen+1];
	int pos = 0;
	if (*str == QLatin1Char('-')) {
		temp[pos++] = '-';
		++str;
	} else if (*str == QLatin1Char('+')) {
		++str;
	}
	while (isDigit(str->unicode()) && pos < maxLen) {
		temp[pos++] = str->toLatin1();
		++str;
	}
	if (*str == QLatin1Char('.') && pos < maxLen) {
		temp[pos++] = '.';
		++str;
	}
	while (isDigit(str->unicode()) && pos < maxLen) {
		temp[pos++] = str->toLatin1();
		++str;
	}
	bool exponent = false;
	if ((*str == QLatin1Char('e') || *str == QLatin1Char('E')) && pos < maxLen) {
		exponent = true;
		temp[pos++] = 'e';
		++str;
		if ((*str == QLatin1Char('-') || *str == QLatin1Char('+')) && pos < maxLen) {
			temp[pos++] = str->toLatin1();
			++str;
		}
		while (isDigit(str->unicode()) && pos < maxLen) {
			temp[pos++] = str->toLatin1();
			++str;
		}
	}
	temp[pos] = '\0';
	qreal val;
	if (!exponent && pos < 10) {
		int ival = 0;
		const char *t = temp;
		bool neg = false;
		if(*t == '-') {
			neg = true;
			++t;
		}
		while(*t && *t != '.') {
			ival *= 10;
			ival += (*t) - '0';
			++t;
		}
		if(*t == '.') {
			++t;
			int div = 1;
			while(*t) {
				ival *= 10;
				ival += (*t) - '0';
				div *= 10;
				++t;
			}
			val = ((qreal)ival)/((qreal)div);
		} else {
			val = ival;
		}
		if (neg)
			val = -val;
	} else {
		val = QByteArray::fromRawData(temp, pos).toDouble();
	}
	return val;
}
/*
static qreal toDouble(const QString &str, bool *ok = nullptr)
{
	const QChar *c = str.constData();
	qreal res = toDouble(c);
	if (ok) {
		*ok = ((*c) == QLatin1Char('\0'));
	}
	return res;
}
*/
static qreal toDouble(const QStringRef &str, bool *ok = nullptr)
{
	const QChar *c = str.constData();
	qreal res = toDouble(c);
	if (ok) {
		*ok = (c == (str.constData() + str.length()));
	}
	return res;
}

static inline void parseNumbersArray(const QChar *&str, QVarLengthArray<qreal, 8> &points)
{
	while (str->isSpace())
		++str;
	while (isDigit(str->unicode()) ||
		   *str == QLatin1Char('-') || *str == QLatin1Char('+') ||
		   *str == QLatin1Char('.')) {
		points.append(toDouble(str));
		while (str->isSpace())
			++str;
		if (*str == QLatin1Char(','))
			++str;
		//eat the rest of space
		while (str->isSpace())
			++str;
	}
}

static QVector<qreal> parseNumbersList(const QChar *&str)
{
	QVector<qreal> points;
	if (!str)
		return points;
	points.reserve(32);
	while (str->isSpace())
		++str;
	while (isDigit(str->unicode()) ||
		   *str == QLatin1Char('-') || *str == QLatin1Char('+') ||
		   *str == QLatin1Char('.')) {
		points.append(toDouble(str));
		while (str->isSpace())
			++str;
		if (*str == QLatin1Char(','))
			++str;
		//eat the rest of space
		while (str->isSpace())
			++str;
	}
	return points;
}

static QVector<qreal> parsePercentageList(const QChar *&str)
{
	QVector<qreal> points;
	if (!str)
		return points;
	while (str->isSpace())
		++str;
	while ((*str >= QLatin1Char('0') && *str <= QLatin1Char('9')) ||
		   *str == QLatin1Char('-') || *str == QLatin1Char('+') ||
		   *str == QLatin1Char('.')) {
		points.append(toDouble(str));
		while (str->isSpace())
			++str;
		if (*str == QLatin1Char('%'))
			++str;
		while (str->isSpace())
			++str;
		if (*str == QLatin1Char(','))
			++str;
		//eat the rest of space
		while (str->isSpace())
			++str;
	}
	return points;
}

static inline int qsvg_h2i(char hex)
{
	if (hex >= '0' && hex <= '9')
		return hex - '0';
	if (hex >= 'a' && hex <= 'f')
		return hex - 'a' + 10;
	if (hex >= 'A' && hex <= 'F')
		return hex - 'A' + 10;
	return -1;
}
static inline int qsvg_hex2int(const char *s)
{
	return (qsvg_h2i(s[0]) << 4) | qsvg_h2i(s[1]);
}
static inline int qsvg_hex2int(char s)
{
	int h = qsvg_h2i(s);
	return (h << 4) | h;
}

bool qsvg_get_hex_rgb(const char *name, QRgb *rgb)
{
	if(name[0] != '#')
		return false;
	name++;
	int len = static_cast<int>(qstrlen(name));
	int r, g, b;
	if (len == 12) {
		r = qsvg_hex2int(name);
		g = qsvg_hex2int(name + 4);
		b = qsvg_hex2int(name + 8);
	} else if (len == 9) {
		r = qsvg_hex2int(name);
		g = qsvg_hex2int(name + 3);
		b = qsvg_hex2int(name + 6);
	} else if (len == 6) {
		r = qsvg_hex2int(name);
		g = qsvg_hex2int(name + 2);
		b = qsvg_hex2int(name + 4);
	} else if (len == 3) {
		r = qsvg_hex2int(name[0]);
		g = qsvg_hex2int(name[1]);
		b = qsvg_hex2int(name[2]);
	} else {
		r = g = b = -1;
	}
	if ((uint)r > 255 || (uint)g > 255 || (uint)b > 255) {
		*rgb = 0;
		return false;
	}
	*rgb = qRgb(r, g ,b);
	return true;
}

bool qsvg_get_hex_rgb(const QChar *str, int len, QRgb *rgb)
{
	if (len > 13)
		return false;
	char tmp[16];
	for(int i = 0; i < len; ++i)
		tmp[i] = str[i].toLatin1();
	tmp[len] = 0;
	return qsvg_get_hex_rgb(tmp, rgb);
}

static QColor parseColor(const QStringRef &color, const QStringRef &opacity)
{
	QColor ret;
	{
		QStringRef color_str = color.trimmed();
		if (color_str.isEmpty())
			return ret;
		switch(color_str.at(0).unicode()) {
		case '#':
		{
			// #rrggbb is very very common, so let's tackle it here
			// rather than falling back to QColor
			QRgb rgb;
			bool ok = qsvg_get_hex_rgb(color_str.unicode(), color_str.length(), &rgb);
			if (ok)
				ret.setRgb(rgb);
			break;
		}
		case 'r':
		{
			// starts with "rgb(", ends with ")" and consists of at least 7 characters "rgb(,,)"
			if (color_str.length() >= 7 && color_str.at(color_str.length() - 1) == QLatin1Char(')')
					&& QStringRef(color_str.string(), color_str.position(), 4) == QLatin1String("rgb(")) {
				const QChar *s = color_str.constData() + 4;
				QVector<qreal> compo = parseNumbersList(s);
				//1 means that it failed after reaching non-parsable
				//character which is going to be "%"
				if (compo.size() == 1) {
					s = color_str.constData() + 4;
					compo = parsePercentageList(s);
					for (int i = 0; i < compo.size(); ++i)
						compo[i] *= (qreal)2.55;
				}
				if (compo.size() == 3) {
					ret = QColor(int(compo[0]),
							int(compo[1]),
							int(compo[2]));
				}
			}
			break;
		}
		case 'c':
			if (color_str == QLatin1String("currentColor")) {
				//color = handler->currentColor();
				return ret;
			}
			break;
		case 'i':
			if (color_str == QLatin1String("inherit"))
				return ret;
			break;
		default:
			ret = QColor(color_str.toString());
			break;
		}
	}
	if (!opacity.isEmpty() && ret.isValid()) {
		bool ok = true;
		qreal op = qMin(qreal(1.0), qMax(qreal(0.0), toDouble(opacity, &ok)));
		if (!ok)
			op = 1.0;
		ret.setAlphaF(op);
	}
	return ret;
}

static QMatrix parseTransformationMatrix(const QStringRef &value)
{
	if (value.isEmpty())
		return QMatrix();
	QMatrix matrix;
	const QChar *str = value.constData();
	const QChar *end = str + value.length();
	while (str < end) {
		if (str->isSpace() || *str == QLatin1Char(',')) {
			++str;
			continue;
		}
		enum State {
			Matrix,
			Translate,
			Rotate,
			Scale,
			SkewX,
			SkewY
		};
		State state = Matrix;
		if (*str == QLatin1Char('m')) {  //matrix
			const char *ident = "atrix";
			for (int i = 0; i < 5; ++i)
				if (*(++str) != QLatin1Char(ident[i]))
					goto error;
			++str;
			state = Matrix;
		} else if (*str == QLatin1Char('t')) { //translate
			const char *ident = "ranslate";
			for (int i = 0; i < 8; ++i)
				if (*(++str) != QLatin1Char(ident[i]))
					goto error;
			++str;
			state = Translate;
		} else if (*str == QLatin1Char('r')) { //rotate
			const char *ident = "otate";
			for (int i = 0; i < 5; ++i)
				if (*(++str) != QLatin1Char(ident[i]))
					goto error;
			++str;
			state = Rotate;
		} else if (*str == QLatin1Char('s')) { //scale, skewX, skewY
			++str;
			if (*str == QLatin1Char('c')) {
				const char *ident = "ale";
				for (int i = 0; i < 3; ++i)
					if (*(++str) != QLatin1Char(ident[i]))
						goto error;
				++str;
				state = Scale;
			} else if (*str == QLatin1Char('k')) {
				if (*(++str) != QLatin1Char('e'))
					goto error;
				if (*(++str) != QLatin1Char('w'))
					goto error;
				++str;
				if (*str == QLatin1Char('X'))
					state = SkewX;
				else if (*str == QLatin1Char('Y'))
					state = SkewY;
				else
					goto error;
				++str;
			} else {
				goto error;
			}
		} else {
			goto error;
		}
		while (str < end && str->isSpace())
			++str;
		if (*str != QLatin1Char('('))
			goto error;
		++str;
		QVarLengthArray<qreal, 8> points;
		parseNumbersArray(str, points);
		if (*str != QLatin1Char(')'))
			goto error;
		++str;
		if(state == Matrix) {
			if(points.count() != 6)
				goto error;
			matrix = QMatrix(points[0], points[1],
					points[2], points[3],
					points[4], points[5]) * matrix;
		} else if (state == Translate) {
			if (points.count() == 1)
				matrix.translate(points[0], 0);
			else if (points.count() == 2)
				matrix.translate(points[0], points[1]);
			else
				goto error;
		} else if (state == Rotate) {
			if(points.count() == 1) {
				matrix.rotate(points[0]);
			} else if (points.count() == 3) {
				matrix.translate(points[1], points[2]);
				matrix.rotate(points[0]);
				matrix.translate(-points[1], -points[2]);
			} else {
				goto error;
			}
		} else if (state == Scale) {
			if (points.count() < 1 || points.count() > 2)
				goto error;
			qreal sx = points[0];
			qreal sy = sx;
			if(points.count() == 2)
				sy = points[1];
			matrix.scale(sx, sy);
		} else if (state == SkewX) {
			if (points.count() != 1)
				goto error;
			const qreal deg2rad = qreal(0.017453292519943295769);
			matrix.shear(qTan(points[0]*deg2rad), 0);
		} else if (state == SkewY) {
			if (points.count() != 1)
				goto error;
			const qreal deg2rad = qreal(0.017453292519943295769);
			matrix.shear(0, qTan(points[0]*deg2rad));
		}
	}
error:
	return matrix;
}

// the arc handling code underneath is from XSVG (BSD license)
/*
 * Copyright  2002 USC/Information Sciences Institute
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without
 * fee, provided that the above copyright notice appear in all copies
 * and that both that copyright notice and this permission notice
 * appear in supporting documentation, and that the name of
 * Information Sciences Institute not be used in advertising or
 * publicity pertaining to distribution of the software without
 * specific, written prior permission.  Information Sciences Institute
 * makes no representations about the suitability of this software for
 * any purpose.  It is provided "as is" without express or implied
 * warranty.
 *
 * INFORMATION SCIENCES INSTITUTE DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS, IN NO EVENT SHALL INFORMATION SCIENCES
 * INSTITUTE BE LIABLE FOR ANY SPECIAL, INDIRECT OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA
 * OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */
static void pathArcSegment(QPainterPath &path,
						   qreal xc, qreal yc,
						   qreal th0, qreal th1,
						   qreal rx, qreal ry, qreal xAxisRotation)
{
	qreal sinTh, cosTh;
	qreal a00, a01, a10, a11;
	qreal x1, y1, x2, y2, x3, y3;
	qreal t;
	qreal thHalf;
	sinTh = qSin(xAxisRotation * (M_PI / 180.0));
	cosTh = qCos(xAxisRotation * (M_PI / 180.0));
	a00 =  cosTh * rx;
	a01 = -sinTh * ry;
	a10 =  sinTh * rx;
	a11 =  cosTh * ry;
	thHalf = 0.5 * (th1 - th0);
	t = (8.0 / 3.0) * qSin(thHalf * 0.5) * qSin(thHalf * 0.5) / qSin(thHalf);
	x1 = xc + qCos(th0) - t * qSin(th0);
	y1 = yc + qSin(th0) + t * qCos(th0);
	x3 = xc + qCos(th1);
	y3 = yc + qSin(th1);
	x2 = x3 + t * qSin(th1);
	y2 = y3 - t * qCos(th1);
	path.cubicTo(a00 * x1 + a01 * y1, a10 * x1 + a11 * y1,
				 a00 * x2 + a01 * y2, a10 * x2 + a11 * y2,
				 a00 * x3 + a01 * y3, a10 * x3 + a11 * y3);
}

static void pathArc(QPainterPath &path,
					qreal rx,
					qreal ry,
					qreal x_axis_rotation,
					int large_arc_flag,
					int sweep_flag,
					qreal x,
					qreal y,
					qreal curx,
					qreal cury)
{
	qreal sin_th, cos_th;
	qreal a00, a01, a10, a11;
	qreal x0, y0, x1, y1, xc, yc;
	qreal d, sfactor, sfactor_sq;
	qreal th0, th1, th_arc;
	int i, n_segs;
	qreal dx, dy, dx1, dy1, Pr1, Pr2, Px, Py, check;
	rx = qAbs(rx);
	ry = qAbs(ry);
	sin_th = qSin(x_axis_rotation * (M_PI / 180.0));
	cos_th = qCos(x_axis_rotation * (M_PI / 180.0));
	dx = (curx - x) / 2.0;
	dy = (cury - y) / 2.0;
	dx1 =  cos_th * dx + sin_th * dy;
	dy1 = -sin_th * dx + cos_th * dy;
	Pr1 = rx * rx;
	Pr2 = ry * ry;
	Px = dx1 * dx1;
	Py = dy1 * dy1;
	/* Spec : check if radii are large enough */
	check = Px / Pr1 + Py / Pr2;
	if (check > 1) {
		rx = rx * qSqrt(check);
		ry = ry * qSqrt(check);
	}
	a00 =  cos_th / rx;
	a01 =  sin_th / rx;
	a10 = -sin_th / ry;
	a11 =  cos_th / ry;
	x0 = a00 * curx + a01 * cury;
	y0 = a10 * curx + a11 * cury;
	x1 = a00 * x + a01 * y;
	y1 = a10 * x + a11 * y;
	/* (x0, y0) is current point in transformed coordinate space.
	   (x1, y1) is new point in transformed coordinate space.
	   The arc fits a unit-radius circle in this space.
	*/
	d = (x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0);
	sfactor_sq = 1.0 / d - 0.25;
	if (sfactor_sq < 0) sfactor_sq = 0;
	sfactor = qSqrt(sfactor_sq);
	if (sweep_flag == large_arc_flag) sfactor = -sfactor;
	xc = 0.5 * (x0 + x1) - sfactor * (y1 - y0);
	yc = 0.5 * (y0 + y1) + sfactor * (x1 - x0);
	/* (xc, yc) is center of the circle. */
	th0 = qAtan2(y0 - yc, x0 - xc);
	th1 = qAtan2(y1 - yc, x1 - xc);
	th_arc = th1 - th0;
	if (th_arc < 0 && sweep_flag)
		th_arc += 2 * M_PI;
	else if (th_arc > 0 && !sweep_flag)
		th_arc -= 2 * M_PI;
	n_segs = qCeil(qAbs(th_arc / (M_PI * 0.5 + 0.001)));
	for (i = 0; i < n_segs; i++) {
		pathArcSegment(path, xc, yc,
					   th0 + i * th_arc / n_segs,
					   th0 + (i + 1) * th_arc / n_segs,
					   rx, ry, x_axis_rotation);
	}
}

static bool parsePathDataFast(const QStringRef &dataStr, QPainterPath &path)
{
	qreal x0 = 0, y0 = 0;              // starting point
	qreal x = 0, y = 0;                // current point
	char lastMode = 0;
	QPointF ctrlPt;
	const QChar *str = dataStr.constData();
	const QChar *end = str + dataStr.size();
	while (str != end) {
		while (str->isSpace())
			++str;
		QChar pathElem = *str;
		++str;
		QChar endc = *end;
		*const_cast<QChar *>(end) = 0; // parseNumbersArray requires 0-termination that QStringRef cannot guarantee
		QVarLengthArray<qreal, 8> arg;
		parseNumbersArray(str, arg);
		*const_cast<QChar *>(end) = endc;
		if (pathElem == QLatin1Char('z') || pathElem == QLatin1Char('Z'))
			arg.append(0);//dummy
		const qreal *num = arg.constData();
		int count = arg.count();
		while (count > 0) {
			qreal offsetX = x;        // correction offsets
			qreal offsetY = y;        // for relative commands
			switch (pathElem.unicode()) {
			case 'm': {
				if (count < 2) {
					num++;
					count--;
					break;
				}
				x = x0 = num[0] + offsetX;
				y = y0 = num[1] + offsetY;
				num += 2;
				count -= 2;
				path.moveTo(x0, y0);
				// As per 1.2  spec 8.3.2 The "moveto" commands
				// If a 'moveto' is followed by multiple pairs of coordinates without explicit commands,
				// the subsequent pairs shall be treated as implicit 'lineto' commands.
				pathElem = QLatin1Char('l');
			}
				break;
			case 'M': {
				if (count < 2) {
					num++;
					count--;
					break;
				}
				x = x0 = num[0];
				y = y0 = num[1];
				num += 2;
				count -= 2;
				path.moveTo(x0, y0);
				// As per 1.2  spec 8.3.2 The "moveto" commands
				// If a 'moveto' is followed by multiple pairs of coordinates without explicit commands,
				// the subsequent pairs shall be treated as implicit 'lineto' commands.
				pathElem = QLatin1Char('L');
			}
				break;
			case 'z':
			case 'Z': {
				x = x0;
				y = y0;
				count--; // skip dummy
				num++;
				path.closeSubpath();
			}
				break;
			case 'l': {
				if (count < 2) {
					num++;
					count--;
					break;
				}
				x = num[0] + offsetX;
				y = num[1] + offsetY;
				num += 2;
				count -= 2;
				path.lineTo(x, y);
			}
				break;
			case 'L': {
				if (count < 2) {
					num++;
					count--;
					break;
				}
				x = num[0];
				y = num[1];
				num += 2;
				count -= 2;
				path.lineTo(x, y);
			}
				break;
			case 'h': {
				x = num[0] + offsetX;
				num++;
				count--;
				path.lineTo(x, y);
			}
				break;
			case 'H': {
				x = num[0];
				num++;
				count--;
				path.lineTo(x, y);
			}
				break;
			case 'v': {
				y = num[0] + offsetY;
				num++;
				count--;
				path.lineTo(x, y);
			}
				break;
			case 'V': {
				y = num[0];
				num++;
				count--;
				path.lineTo(x, y);
			}
				break;
			case 'c': {
				if (count < 6) {
					num += count;
					count = 0;
					break;
				}
				QPointF c1(num[0] + offsetX, num[1] + offsetY);
				QPointF c2(num[2] + offsetX, num[3] + offsetY);
				QPointF e(num[4] + offsetX, num[5] + offsetY);
				num += 6;
				count -= 6;
				path.cubicTo(c1, c2, e);
				ctrlPt = c2;
				x = e.x();
				y = e.y();
				break;
			}
			case 'C': {
				if (count < 6) {
					num += count;
					count = 0;
					break;
				}
				QPointF c1(num[0], num[1]);
				QPointF c2(num[2], num[3]);
				QPointF e(num[4], num[5]);
				num += 6;
				count -= 6;
				path.cubicTo(c1, c2, e);
				ctrlPt = c2;
				x = e.x();
				y = e.y();
				break;
			}
			case 's': {
				if (count < 4) {
					num += count;
					count = 0;
					break;
				}
				QPointF c1;
				if (lastMode == 'c' || lastMode == 'C' ||
						lastMode == 's' || lastMode == 'S')
					c1 = QPointF(2*x-ctrlPt.x(), 2*y-ctrlPt.y());
				else
					c1 = QPointF(x, y);
				QPointF c2(num[0] + offsetX, num[1] + offsetY);
				QPointF e(num[2] + offsetX, num[3] + offsetY);
				num += 4;
				count -= 4;
				path.cubicTo(c1, c2, e);
				ctrlPt = c2;
				x = e.x();
				y = e.y();
				break;
			}
			case 'S': {
				if (count < 4) {
					num += count;
					count = 0;
					break;
				}
				QPointF c1;
				if (lastMode == 'c' || lastMode == 'C' ||
						lastMode == 's' || lastMode == 'S')
					c1 = QPointF(2*x-ctrlPt.x(), 2*y-ctrlPt.y());
				else
					c1 = QPointF(x, y);
				QPointF c2(num[0], num[1]);
				QPointF e(num[2], num[3]);
				num += 4;
				count -= 4;
				path.cubicTo(c1, c2, e);
				ctrlPt = c2;
				x = e.x();
				y = e.y();
				break;
			}
			case 'q': {
				if (count < 4) {
					num += count;
					count = 0;
					break;
				}
				QPointF c(num[0] + offsetX, num[1] + offsetY);
				QPointF e(num[2] + offsetX, num[3] + offsetY);
				num += 4;
				count -= 4;
				path.quadTo(c, e);
				ctrlPt = c;
				x = e.x();
				y = e.y();
				break;
			}
			case 'Q': {
				if (count < 4) {
					num += count;
					count = 0;
					break;
				}
				QPointF c(num[0], num[1]);
				QPointF e(num[2], num[3]);
				num += 4;
				count -= 4;
				path.quadTo(c, e);
				ctrlPt = c;
				x = e.x();
				y = e.y();
				break;
			}
			case 't': {
				if (count < 2) {
					num += count;
					count = 0;
					break;
				}
				QPointF e(num[0] + offsetX, num[1] + offsetY);
				num += 2;
				count -= 2;
				QPointF c;
				if (lastMode == 'q' || lastMode == 'Q' ||
						lastMode == 't' || lastMode == 'T')
					c = QPointF(2*x-ctrlPt.x(), 2*y-ctrlPt.y());
				else
					c = QPointF(x, y);
				path.quadTo(c, e);
				ctrlPt = c;
				x = e.x();
				y = e.y();
				break;
			}
			case 'T': {
				if (count < 2) {
					num += count;
					count = 0;
					break;
				}
				QPointF e(num[0], num[1]);
				num += 2;
				count -= 2;
				QPointF c;
				if (lastMode == 'q' || lastMode == 'Q' ||
						lastMode == 't' || lastMode == 'T')
					c = QPointF(2*x-ctrlPt.x(), 2*y-ctrlPt.y());
				else
					c = QPointF(x, y);
				path.quadTo(c, e);
				ctrlPt = c;
				x = e.x();
				y = e.y();
				break;
			}
			case 'a': {
				if (count < 7) {
					num += count;
					count = 0;
					break;
				}
				qreal rx = (*num++);
				qreal ry = (*num++);
				qreal xAxisRotation = (*num++);
				qreal largeArcFlag  = (*num++);
				qreal sweepFlag = (*num++);
				qreal ex = (*num++) + offsetX;
				qreal ey = (*num++) + offsetY;
				count -= 7;
				qreal curx = x;
				qreal cury = y;
				pathArc(path, rx, ry, xAxisRotation, int(largeArcFlag),
						int(sweepFlag), ex, ey, curx, cury);
				x = ex;
				y = ey;
			}
				break;
			case 'A': {
				if (count < 7) {
					num += count;
					count = 0;
					break;
				}
				qreal rx = (*num++);
				qreal ry = (*num++);
				qreal xAxisRotation = (*num++);
				qreal largeArcFlag  = (*num++);
				qreal sweepFlag = (*num++);
				qreal ex = (*num++);
				qreal ey = (*num++);
				count -= 7;
				qreal curx = x;
				qreal cury = y;
				pathArc(path, rx, ry, xAxisRotation, int(largeArcFlag),
						int(sweepFlag), ex, ey, curx, cury);
				x = ex;
				y = ey;
			}
				break;
			default:
				return false;
			}
			lastMode = pathElem.toLatin1();
		}
	}
	return true;
}

SvgHandler::SvgHandler(QGraphicsScene *scene)
	: m_scene(scene)
{
}

void SvgHandler::load(QXmlStreamReader *data)
{
	m_xml = data;
	m_defaultPen = QPen(Qt::black, 1, Qt::SolidLine, Qt::FlatCap, Qt::SvgMiterJoin);
	m_defaultPen.setMiterLimit(4);
	parse();
	/*
	QGraphicsRectItem *it = new QGraphicsRectItem();
	it->setRect(m_scene->sceneRect());
	it->setPen(QPen(Qt::blue));
	m_scene->addItem(it);
	*/
}

void SvgHandler::parse()
{
	QStack<bool> item_creations;
	m_xml->setNamespaceProcessing(false);
	bool done = false;
	while (!m_xml->atEnd() && !done) {
		switch (m_xml->readNext()) {
		case QXmlStreamReader::StartElement:
			logSvgD() << "start element:" << m_xml->name();
			if (startElement(m_xml->name().toString(), m_xml->attributes())) {
				item_creations.push(true);
			}
			else {
				item_creations.push(false);
			}
			break;
		case QXmlStreamReader::EndElement: {
			bool item_created = item_creations.pop();
			logSvgD() << "end element:" << m_xml->name() << "item created:" << item_created;
			if(item_created && m_topLevelItem)
				m_topLevelItem = m_topLevelItem->parentItem();
			break;
		}
		case QXmlStreamReader::Characters:
			logSvgD() << "characters element:" << m_xml->text();
			break;
		case QXmlStreamReader::ProcessingInstruction:
			logSvgD() << "ProcessingInstruction:" << m_xml->processingInstructionTarget() << m_xml->processingInstructionData();
			//processingInstruction(xml->processingInstructionTarget().toString(), xml->processingInstructionData().toString());
			break;
		default:
			break;
		}
	}
}

bool SvgHandler::startElement(const QString &element_name, const QXmlStreamAttributes &attributes)
{
	if (!m_topLevelItem) {
		if (element_name == QLatin1String("svg")) {
			m_topLevelItem = new QGraphicsRectItem();
			m_scene->addItem(m_topLevelItem);
			return true;
		}
		else {
			nWarning() << "unsupported root element:" << element_name;
		}
	}
	else {
		if (element_name == QLatin1String("g")) {
			auto *g = new QGraphicsRectItem();
			parseStyle(g, attributes);
			parseTransform(g, attributes);
			addItem(g);
			//g->setRotation(45);
			return true;
		}
		else if (element_name == QLatin1String("rect")) {
			qreal x = attributes.value(QLatin1String("x")).toDouble();
			qreal y = attributes.value(QLatin1String("y")).toDouble();
			qreal w = attributes.value(QLatin1String("width")).toDouble();
			qreal h = attributes.value(QLatin1String("height")).toDouble();
			QGraphicsRectItem *rect = new QGraphicsRectItem();
			QStringRef style = attributes.value(QLatin1String("style"));
			QVector<QStringRef> css = style.split(';', QString::SkipEmptyParts);
			for(QStringRef ss : css) {
				static auto FILL = QStringLiteral("fill:");
				if(ss.startsWith(FILL)) {
					rect->setBrush(QColor(ss.mid(FILL.length())));

				}
			}
			rect->setRect(QRectF(x, y, w, h));
			parseStyle(rect, attributes);
			parseTransform(rect, attributes);
			addItem(rect);
			return true;
		}
		else if (element_name == QLatin1String("circle")) {
			qreal cx = attributes.value(QLatin1String("cx")).toDouble();
			qreal cy = attributes.value(QLatin1String("cy")).toDouble();
			qreal rx = attributes.value(QLatin1String("r")).toDouble();
			QRectF r(0, 0, 2*rx, 2*rx);
			r.translate(cx - rx, cy - rx);
			QGraphicsEllipseItem *elipse = new QGraphicsEllipseItem();
			elipse->setRect(r);
			parseStyle(elipse, attributes);
			parseTransform(elipse, attributes);
			addItem(elipse);
			return true;
		}
		else if (element_name == QLatin1String("ellipse")) {
			qreal cx = attributes.value(QLatin1String("cx")).toDouble();
			qreal cy = attributes.value(QLatin1String("cy")).toDouble();
			qreal rx = attributes.value(QLatin1String("rx")).toDouble();
			qreal ry = attributes.value(QLatin1String("ry")).toDouble();
			QRectF r(0, 0, 2*rx, 2*ry);
			r.translate(cx - rx, cy - ry);
			QGraphicsEllipseItem *elipse = new QGraphicsEllipseItem();
			elipse->setRect(r);
			parseStyle(elipse, attributes);
			parseTransform(elipse, attributes);
			addItem(elipse);
			return true;
		}
		else if (element_name == QLatin1String("path")) {
			QStringRef data = attributes.value(QLatin1String("d"));
			QPainterPath p;
			p.setFillRule(Qt::WindingFill);
			//XXX do error handling
			parsePathDataFast(data, p);
			QGraphicsPathItem *path = new QGraphicsPathItem();
			path->setPath(p);
			parseStyle(path, attributes);
			parseTransform(path, attributes);
			addItem(path);
			return true;
		}
		else {
			nWarning() << "unsupported element:" << element_name;
		}
	}
	return false;
}

void SvgHandler::parseTransform(QGraphicsItem *it, const QXmlStreamAttributes &attributes)
{
	QStringRef transform = attributes.value(QLatin1String("transform"));
	QMatrix mx = parseTransformationMatrix(transform.trimmed());
	if(!mx.isIdentity()) {
		QTransform t(mx);
		logSvgI() << typeid (*it).name() << "setting matrix:" << t.dx() << t.dy();
		it->setTransform(t);
	}
}

void SvgHandler::parseStyle(QAbstractGraphicsShapeItem *it, const QXmlStreamAttributes &attributes)
{
	QStringRef style = attributes.value(QLatin1String("style"));
	QVector<QStringRef> css = style.split(';', QString::SkipEmptyParts);
	QMap<QStringRef, QStringRef> attrs;
	for(QStringRef ss : css) {
		int ix = ss.indexOf(':');
		if(ix > 0) {
			attrs[ss.mid(0, ix)] = ss.mid(ix + 1);
		}
	}
	static auto FILL = QStringLiteral("fill");
	QStringRef fill = attrs.value(QStringRef(&FILL));
	if(fill.isEmpty() || fill == QLatin1String("none")) {
		it->setBrush(Qt::NoBrush);
	}
	else {
		static auto FILL_OPACITY = QStringLiteral("fill-opacity");
		QStringRef opacity = attrs.value(QStringRef(&FILL_OPACITY));
		it->setBrush(parseColor(fill, opacity));
	}
	static auto STROKE = QStringLiteral("stroke");
	QStringRef stroke = attrs.value(QStringRef(&STROKE));
	if(stroke.isEmpty() || stroke == QLatin1String("none")) {
		it->setPen(Qt::NoPen);
	}
	else {
		static auto STROKE_WIDTH = QStringLiteral("stroke-width");
		static auto STROKE_OPACITY = QStringLiteral("stroke-opacity");
		QStringRef opacity = attrs.value(QStringRef(&STROKE_OPACITY));
		QPen pen(parseColor(stroke, opacity));
		pen.setWidthF(toDouble(attrs.value(QStringRef(&STROKE_WIDTH))));
		it->setPen(pen);
	}
}

void SvgHandler::addItem(QGraphicsItem *it)
{
	if(!m_topLevelItem)
		return;
	logSvgI() << "adding element:" << typeid (*it).name() << it;
	if(QGraphicsItemGroup *grp = dynamic_cast<QGraphicsItemGroup*>(m_topLevelItem)) {
		grp->addToGroup(it);
	}
	else {
		it->setParentItem(m_topLevelItem);
	}
	m_topLevelItem = it;
}
