#ifndef SVGHANDLER_H
#define SVGHANDLER_H

#include <QPen>
#include <QMap>
#include <QStack>

class QXmlStreamReader;
class QXmlStreamAttributes;
class QGraphicsScene;
class QGraphicsItem;
class QGraphicsSimpleTextItem;
class QGraphicsTextItem;
class QAbstractGraphicsShapeItem;
//class QGraphicsItemGroup;

class SvgHandler
{
public:
	SvgHandler(QGraphicsScene *scene);
	void load(QXmlStreamReader *data);
private:
	void parse();
	using XmlAttributes = QMap<QString, QString>;
	using CssAttributes = QMap<QString, QString>;
	XmlAttributes parseXmlAttributes(const QXmlStreamAttributes &attributes);
	void mergeCSSAttributes(CssAttributes &css_attributes, const QString &attr_name, const XmlAttributes &xml_attributes);

	void setTransform(QGraphicsItem *it, const QString &str_val);
	void setStyle(QAbstractGraphicsShapeItem *it, const CssAttributes &attributes);
	void setTextStyle(QFont &font, const CssAttributes &attributes);
	void setTextStyle(QGraphicsSimpleTextItem *text, const CssAttributes &attributes);
	void setTextStyle(QGraphicsTextItem *text, const CssAttributes &attributes);

	bool startElement();
	void addItem(QGraphicsItem *it);
private:
	struct SvgElement
	{
		QString name;
		XmlAttributes xmlAttributes;
		CssAttributes styleAttributes;
		bool itemCreated = false;

		SvgElement() {}
		SvgElement(const QString &n, bool created = false) : name(n), itemCreated(created) {}
	};
	QStack<SvgElement> m_elementStack;

	QGraphicsScene *m_scene;
	//QGraphicsItemGroup *m_topLevelGroup = nullptr;
	QGraphicsItem *m_topLevelItem = nullptr;
	QXmlStreamReader *m_xml = nullptr;
	QPen m_defaultPen;
};

#endif // SVGHANDLER_H
