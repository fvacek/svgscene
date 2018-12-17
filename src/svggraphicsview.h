#ifndef SVGGRAPHICSVIEW_H
#define SVGGRAPHICSVIEW_H

#include <QGraphicsView>

class SvgGraphicsView : public QGraphicsView
{
	Q_OBJECT

	using Super = QGraphicsView;
public:
	SvgGraphicsView(QWidget *parent = nullptr);

	void zoomToFit();
protected:
	void zoom(double delta, const QPoint &mouse_pos);

	void paintEvent(QPaintEvent *event) override;

	void wheelEvent(QWheelEvent *ev) Q_DECL_OVERRIDE;
	void mousePressEvent(QMouseEvent *ev) Q_DECL_OVERRIDE;
	void mouseReleaseEvent(QMouseEvent *ev) Q_DECL_OVERRIDE;
	void mouseMoveEvent(QMouseEvent *ev) Q_DECL_OVERRIDE;
private:
	QPoint m_dragMouseStartPos;
};

#endif // SVGGRAPHICSVIEW_H
