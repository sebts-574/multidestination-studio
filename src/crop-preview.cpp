/*
Multidestination Studio
Copyright (C) 2026 Sébastien Tombre

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

#include "crop-preview.hpp"
#include "render-helpers.hpp"
#include "scene-editor.hpp"

#include <QMouseEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr uint32_t SHADE_COLOR = 0xB0000000;
constexpr uint32_t FRAME_COLOR = 0xFF2F86FF;
constexpr uint32_t HANDLE_COLOR = 0xFFFFFFFF;
constexpr float HANDLE_PX = 8.0f;
constexpr float TOLERANCE_PX = 10.0f;
} // namespace

CropPreview::CropPreview(obs_source_t *source_, QWidget *parent) : OBSQTDisplay(parent), source(source_)
{
	setMouseTracking(true);
	setMinimumSize(480, 270);
	if (source)
		obs_source_inc_showing(source);
	connect(this, &OBSQTDisplay::DisplayCreated, this,
		[this](OBSQTDisplay *w) { obs_display_add_draw_callback(w->GetDisplay(), Draw, this); });
}

CropPreview::~CropPreview()
{
	if (obs_display_t *d = GetDisplay())
		obs_display_remove_draw_callback(d, Draw, this);
	if (source)
		obs_source_dec_showing(source);
}

void CropPreview::SetCrop(const struct obs_sceneitem_crop &crop)
{
	left = crop.left;
	top = crop.top;
	right = crop.right;
	bottom = crop.bottom;
}

struct obs_sceneitem_crop CropPreview::Crop() const
{
	return {left.load(), top.load(), right.load(), bottom.load()};
}

void CropPreview::Draw(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<CropPreview *>(data);
	obs_source_t *src = self->source;
	const uint32_t w = src ? obs_source_get_width(src) : 0;
	const uint32_t h = src ? obs_source_get_height(src) : 0;
	if (!w || !h)
		return;
	const struct obs_sceneitem_crop c = self->Crop();
	DrawFitted(w, h, cx, cy, [&](float scale) {
		const float px = 1.0f / scale;
		const float fw = float(w);
		const float fh = float(h);
		DrawSolidRect(0.0f, 0.0f, fw, fh, 0xFF000000);
		const bool previous = gs_set_linear_srgb(true);
		obs_source_video_render(src);
		gs_set_linear_srgb(previous);

		const float l = float(c.left), t = float(c.top), r = float(c.right), b = float(c.bottom);
		const float iw = std::max(0.0f, fw - l - r);
		const float ih = std::max(0.0f, fh - t - b);
		/* Zones rognées assombries. */
		DrawSolidRect(0.0f, 0.0f, l, fh, SHADE_COLOR);
		DrawSolidRect(fw - r, 0.0f, r, fh, SHADE_COLOR);
		DrawSolidRect(l, 0.0f, iw, t, SHADE_COLOR);
		DrawSolidRect(l, fh - b, iw, b, SHADE_COLOR);
		/* Cadre gardé et ses poignées. */
		DrawOutline(l, t, iw, ih, 2.0f * px, FRAME_COLOR);
		const float hs = HANDLE_PX * px;
		const float xs[3] = {l, l + iw * 0.5f, l + iw};
		const float ys[3] = {t, t + ih * 0.5f, t + ih};
		for (int ix = 0; ix < 3; ix++) {
			for (int iy = 0; iy < 3; iy++) {
				if (ix == 1 && iy == 1)
					continue;
				DrawSolidRect(xs[ix] - hs * 0.5f - px, ys[iy] - hs * 0.5f - px, hs + 2.0f * px,
					      hs + 2.0f * px, FRAME_COLOR);
				DrawSolidRect(xs[ix] - hs * 0.5f, ys[iy] - hs * 0.5f, hs, hs, HANDLE_COLOR);
			}
		}
	});
}

bool CropPreview::ToSource(const QPointF &pos, float &x, float &y, float &tolerance) const
{
	const uint32_t w = source ? obs_source_get_width(source) : 0;
	const uint32_t h = source ? obs_source_get_height(source) : 0;
	if (!w || !h)
		return false;
	const qreal ratio = devicePixelRatioF();
	const QSize size = this->size() * ratio;
	const FitRect fit = ComputeFit(w, h, uint32_t(size.width()), uint32_t(size.height()));
	if (fit.scale <= 0.0f)
		return false;
	x = (float(pos.x() * ratio) - float(fit.x)) / fit.scale;
	y = (float(pos.y() * ratio) - float(fit.y)) / fit.scale;
	tolerance = TOLERANCE_PX * float(ratio) / fit.scale;
	return true;
}

void CropPreview::mousePressEvent(QMouseEvent *event)
{
	float x, y, tol;
	if (event->button() != Qt::LeftButton || !ToSource(event->position(), x, y, tol))
		return;
	const uint32_t w = obs_source_get_width(source);
	const uint32_t h = obs_source_get_height(source);
	const struct obs_sceneitem_crop c = Crop();
	const SceneEditor::Box box{float(c.left), float(c.top), float(int(w) - c.left - c.right),
				   float(int(h) - c.top - c.bottom)};
	dragEdges = SceneEditor::HandleAt(box, x, y, tol);
	if (dragEdges == SceneEditor::EDGE_NONE && !box.Contains(x, y)) {
		dragEdges = -1;
		return;
	}
	startX = x;
	startY = y;
	startCrop = c;
}

void CropPreview::mouseMoveEvent(QMouseEvent *event)
{
	float x, y, tol;
	if (!ToSource(event->position(), x, y, tol))
		return;
	const int w = int(obs_source_get_width(source));
	const int h = int(obs_source_get_height(source));
	const struct obs_sceneitem_crop s = startCrop;

	if (dragEdges < 0) {
		const struct obs_sceneitem_crop c = Crop();
		const SceneEditor::Box now{float(c.left), float(c.top), float(w - c.left - c.right),
					   float(h - c.top - c.bottom)};
		const int edges = SceneEditor::HandleAt(now, x, y, tol);
		const bool lr = edges & (SceneEditor::EDGE_LEFT | SceneEditor::EDGE_RIGHT);
		const bool tb = edges & (SceneEditor::EDGE_TOP | SceneEditor::EDGE_BOTTOM);
		const bool diagMain = edges == (SceneEditor::EDGE_LEFT | SceneEditor::EDGE_TOP) ||
				      edges == (SceneEditor::EDGE_RIGHT | SceneEditor::EDGE_BOTTOM);
		if (lr && tb)
			setCursor(diagMain ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
		else if (lr)
			setCursor(Qt::SizeHorCursor);
		else if (tb)
			setCursor(Qt::SizeVerCursor);
		else if (now.Contains(x, y))
			setCursor(Qt::SizeAllCursor);
		else
			unsetCursor();
		return;
	}

	const int dx = int(std::lround(x - startX));
	const int dy = int(std::lround(y - startY));
	struct obs_sceneitem_crop c = s;
	if (dragEdges == SceneEditor::EDGE_NONE) {
		/* Tout le cadre : même taille, déplacé dans la source. */
		c.left = std::clamp(s.left + dx, 0, s.left + s.right);
		c.right = s.left + s.right - c.left;
		c.top = std::clamp(s.top + dy, 0, s.top + s.bottom);
		c.bottom = s.top + s.bottom - c.top;
	} else {
		if (dragEdges & SceneEditor::EDGE_LEFT)
			c.left = std::clamp(s.left + dx, 0, w - s.right - 1);
		if (dragEdges & SceneEditor::EDGE_RIGHT)
			c.right = std::clamp(s.right - dx, 0, w - s.left - 1);
		if (dragEdges & SceneEditor::EDGE_TOP)
			c.top = std::clamp(s.top + dy, 0, h - s.bottom - 1);
		if (dragEdges & SceneEditor::EDGE_BOTTOM)
			c.bottom = std::clamp(s.bottom - dy, 0, h - s.top - 1);
	}
	SetCrop(c);
	if (onChanged)
		onChanged(c);
}

void CropPreview::mouseReleaseEvent(QMouseEvent *)
{
	dragEdges = -1;
}
