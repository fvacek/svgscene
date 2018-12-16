#ifndef SVGHANDLER_H
#define SVGHANDLER_H

#include <QPen>

class QXmlStreamReader;
class QXmlStreamAttributes;
class QGraphicsScene;
class QGraphicsItem;
class QAbstractGraphicsShapeItem;
//class QGraphicsItemGroup;

class SvgHandler
{
public:
	SvgHandler(QGraphicsScene *scene);
	void load(QXmlStreamReader *data);
private:
	void parse();
	void parseTransform(QGraphicsItem *it, const QXmlStreamAttributes &attributes);
	void parseStyle(QAbstractGraphicsShapeItem *it, const QXmlStreamAttributes &attributes);
	bool startElement(const QString &localName, const QXmlStreamAttributes &attributes);
	void addItem(QGraphicsItem *it);
private:
	QGraphicsScene *m_scene;
	//QGraphicsItemGroup *m_topLevelGroup = nullptr;
	QGraphicsItem *m_topLevelItem = nullptr;
	QXmlStreamReader *m_xml = nullptr;
	QPen m_defaultPen;
};

#endif // SVGHANDLER_H
