/*
Multidestination Studio
Copyright (C) 2026 sebts

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include "qt-display.hpp"

#include <obs.hpp>

#include <atomic>
#include <functional>

/* Aperçu d'une source entière avec son rognage : les zones rognées sont
 * assombries, et le cadre se règle à la souris (bords, coins, ou tout le
 * cadre en le faisant glisser). */
class CropPreview : public OBSQTDisplay {
	Q_OBJECT

public:
	CropPreview(obs_source_t *source, QWidget *parent);
	~CropPreview() override;

	void SetCrop(const struct obs_sceneitem_crop &crop);
	/* Rognage modifié à la souris. */
	std::function<void(const struct obs_sceneitem_crop &)> onChanged;

protected:
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;

private:
	static void Draw(void *data, uint32_t cx, uint32_t cy);
	bool ToSource(const QPointF &pos, float &x, float &y, float &tolerance) const;
	struct obs_sceneitem_crop Crop() const;

	OBSSource source;
	std::atomic<int> left{0}, top{0}, right{0}, bottom{0};
	int dragEdges = -1; /* -1 : pas de glisser ; 0 : tout le cadre */
	float startX = 0.0f, startY = 0.0f;
	struct obs_sceneitem_crop startCrop = {};
};
