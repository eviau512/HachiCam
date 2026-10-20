"""Independent, offline block matching + robust LK experiment (not app code).

Inputs are equal-size 8-bit grayscale patches after global registration. Mapping:
source(x + dx, y + dy) corresponds to reference(x, y). Gain/bias are geometry-only
nuisance variables. Original image observations are never changed by this module.
"""
from dataclasses import asdict, dataclass
import math

import cv2
import numpy as np


@dataclass(frozen=True)
class Config:
    max_shift: int = 8
    sigma: float = 1.2
    iterations: int = 15
    huber: float = 8.0
    min_samples: int = 256
    min_eigenvalue: float = 0.2
    min_eigen_ratio: float = 0.01
    min_peak_margin: float = 0.08
    max_fb_error: float = 0.2
    max_residual: float = 8.0
    min_inlier_fraction: float = 0.8


@dataclass
class Result:
    accepted: bool = False
    reason: str = "not_evaluated"
    dx: float = 0.0
    dy: float = 0.0
    eigen_min: float = 0.0
    eigen_ratio: float = 0.0
    peak_margin: float = 0.0
    residual: float = 0.0
    inlier_fraction: float = 0.0
    fb_error: float = 0.0
    gain: float = 1.0
    bias: float = 0.0

    def record(self):
        return asdict(self)


def _sample(image, dx, dy):
    y, x = np.indices(image.shape, dtype=np.float32)
    # Explicit bilinear interpolation avoids OpenCV remap's 1/32-pixel table.
    xx, yy = x + dx, y + dy
    valid = (xx >= 0) & (yy >= 0) & (xx < image.shape[1]-1) & (yy < image.shape[0]-1)
    ix = np.clip(np.floor(xx).astype(int), 0, image.shape[1]-2)
    iy = np.clip(np.floor(yy).astype(int), 0, image.shape[0]-2)
    fx, fy = xx-ix, yy-iy
    sampled = ((1-fy)*((1-fx)*image[iy, ix]+fx*image[iy, ix+1]) +
               fy*((1-fx)*image[iy+1, ix]+fx*image[iy+1, ix+1]))
    return sampled, valid


def _photometric(target, observed, weights=None):
    if weights is None:
        weights = np.ones(target.shape)
    total = weights.sum()
    mt, mo = (weights*target).sum()/total, (weights*observed).sum()/total
    variance = (weights*(observed-mo)**2).sum()
    gain = np.clip((weights*(target-mt)*(observed-mo)).sum()/max(variance, 1e-9), .25, 4.)
    bias = mt-gain*mo
    return gain, bias, gain*observed+bias-target


def _hessian(gx, gy, weights=None):
    if weights is None:
        weights = np.ones(gx.shape)
    h = np.array([[(weights*gx*gx).sum(), (weights*gx*gy).sum()],
                  [(weights*gx*gy).sum(), (weights*gy*gy).sum()]])
    eigen = np.linalg.eigvalsh(h/max(weights.sum(), 1.))
    return h, float(eigen[0]), float(eigen[0]/max(eigen[1], 1e-9))


def _integer_costs(reference, source, common, bound):
    """Affine-photometric SSD for all integer shifts via native correlations."""
    yy, xx = np.where(common)
    x0, x1, y0, y1 = xx.min(), xx.max()+1, yy.min(), yy.max()+1
    template = reference[y0:y1, x0:x1].copy()
    mask = common[y0:y1, x0:x1].astype(np.float32)
    n = float(mask.sum())
    st, stt = float((template*mask).sum()), float((template*template*mask).sum())
    ss = cv2.matchTemplate(source, mask, cv2.TM_CCORR)
    sss = cv2.matchTemplate(source*source, mask, cv2.TM_CCORR)
    cross = cv2.matchTemplate(source, template*mask, cv2.TM_CCORR)
    vs = np.maximum(sss-ss*ss/n, 1e-5)
    covariance = cross-ss*st/n
    gain = np.clip(covariance/vs, .25, 4.)
    cost = np.maximum(0., (stt-st*st/n+gain*gain*vs-2*gain*covariance)/n)
    return [(float(cost[y0+dy, x0+dx]), dx, dy)
            for dy in range(-bound, bound+1) for dx in range(-bound, bound+1)]


def _fit(reference, source, ref_valid, src_valid, cfg):
    result = Result()
    r = cv2.GaussianBlur(reference.astype(np.float32), (0, 0), cfg.sigma)
    s = cv2.GaussianBlur(source.astype(np.float32), (0, 0), cfg.sigma)
    margin = cfg.max_shift+4
    valid = ref_valid.copy()
    valid[:margin] = valid[-margin:] = False
    valid[:, :margin] = valid[:, -margin:] = False
    if valid.sum() < cfg.min_samples:
        result.reason = "insufficient_support"
        return result
    gy, gx = np.gradient(r)
    _, result.eigen_min, result.eigen_ratio = _hessian(gx[valid], gy[valid])
    if result.eigen_min < cfg.min_eigenvalue or result.eigen_ratio < cfg.min_eigen_ratio:
        result.reason = "flat_or_aperture"
        return result

    # Half-resolution exhaustive initialization. Uniqueness is checked at the
    # full-resolution integer grid after refinement, so downsampling ambiguity
    # does not silently become valid two-dimensional phase evidence.
    half_r, half_s = cv2.pyrDown(r), cv2.pyrDown(s)
    half_valid = cv2.resize(valid.astype(np.uint8), (half_r.shape[1], half_r.shape[0]),
                            interpolation=cv2.INTER_NEAREST).astype(bool)
    half_src_valid = cv2.resize(src_valid.astype(np.uint8), (half_r.shape[1], half_r.shape[0]),
                                interpolation=cv2.INTER_NEAREST).astype(bool)
    bound = int(math.ceil(cfg.max_shift/2))
    half_common = half_valid & cv2.erode(half_src_valid.astype(np.uint8),
                                          np.ones((2*bound+3,)*2, np.uint8)).astype(bool)
    if half_common.sum() < cfg.min_samples//4:
        result.reason = "insufficient_support"
        return result
    costs = _integer_costs(half_r, half_s, half_common, bound)
    _, dx, dy = min(costs)
    delta = np.array([dx, dy], dtype=float)
    for level_r, level_s, level_valid, level_src_valid, scale in (
            (half_r, half_s, half_valid, half_src_valid, 2), (r, s, valid, src_valid, 1)):
        if scale == 1:
            delta *= 2
        sy, sx = np.gradient(level_s)
        for _ in range(cfg.iterations):
            observed, inside = _sample(level_s, *delta)
            sampled_valid, _ = _sample(level_src_valid.astype(float), *delta)
            use = level_valid & inside & (sampled_valid > .999)
            if use.sum() < cfg.min_samples//(scale*scale):
                result.reason = "insufficient_support"
                return result
            gain, bias, residual = _photometric(level_r[use], observed[use])
            weights = np.minimum(1., cfg.huber/np.maximum(np.abs(residual), 1e-9))
            gain, bias, residual = _photometric(level_r[use], observed[use], weights)
            jx, _ = _sample(sx, *delta)
            jy, _ = _sample(sy, *delta)
            jx, jy = gain*jx[use], gain*jy[use]
            h, _, _ = _hessian(jx, jy, weights)
            try:
                step = np.linalg.solve(h, [np.sum(weights*jx*residual), np.sum(weights*jy*residual)])
            except np.linalg.LinAlgError:
                result.reason = "singular_fit"
                return result
            if not np.isfinite(step).all():
                result.reason = "nonfinite_fit"
                return result
            step /= max(1., np.linalg.norm(step))
            delta -= step
            if np.linalg.norm(delta)*scale > cfg.max_shift:
                result.reason = "displacement_bound"
                return result
            if np.linalg.norm(step) < .001:
                break

    return _assess(r, s, valid, src_valid, cfg, delta, result)


def _assess(r, s, valid, src_valid, cfg, delta, result):
    result.dx, result.dy = map(float, delta)
    observed, inside = _sample(s, *delta)
    observed_valid, _ = _sample(src_valid.astype(float), *delta)
    use = valid & inside & (observed_valid > .999)
    if use.sum() < cfg.min_samples:
        result.reason = "insufficient_support"
        return result
    result.gain, result.bias, residual = _photometric(r[use], observed[use])
    result.residual = float(np.sqrt(np.mean(np.minimum(residual**2, cfg.huber**2))))
    result.inlier_fraction = float(np.mean(np.abs(residual) <= cfg.max_residual))

    # Peak uniqueness uses a common valid support across *all* candidate shifts.
    # Otherwise clipped samples can make incomparable costs appear distinctive.
    common = valid & inside
    kernel = np.ones((2*cfg.max_shift+3,)*2, np.uint8)
    common &= cv2.erode(src_valid.astype(np.uint8), kernel).astype(bool)
    if common.sum() < cfg.min_samples:
        result.reason = "insufficient_common_support"
        return result
    # Competing peaks must share the fitted fractional phase. Comparing a
    # subpixel minimum only with integer samples spuriously certifies periodic
    # grids because the integer alternatives have extra interpolation error.
    fractional = delta-np.round(delta)
    fractional_source, _ = _sample(s, *fractional)
    costs = _integer_costs(r, fractional_source.astype(np.float32), common, cfg.max_shift)
    _, _, best_residual = _photometric(r[common], observed[common])
    best = float(np.mean(best_residual**2))
    distant = [cost for cost, dx, dy in costs
               if np.hypot(dx+fractional[0]-delta[0], dy+fractional[1]-delta[1]) >= 2.]
    second = min(distant)
    result.peak_margin = (second-best)/max(second, 1e-9)
    if result.peak_margin < cfg.min_peak_margin:
        result.reason = "ambiguous_peak"
    elif result.inlier_fraction < cfg.min_inlier_fraction:
        result.reason = "residual_support"
    else:
        result.accepted, result.reason = True, "accepted"
    return result


def align_patch(reference, source, cfg=Config()):
    """Return a patch-level diagnostic; no dense field, pixel gate or SR renderer."""
    if (reference.dtype != np.uint8 or source.dtype != np.uint8 or
            reference.ndim != 2 or reference.shape != source.shape or
            min(reference.shape) < 64 or max(reference.shape) > 512 or
            cfg.max_shift < 1 or cfg.max_shift > 16):
        raise ValueError("Expected equal 64..512 grayscale uint8 patches and a bounded search")
    # Dilate invalid pixels to keep smoothing from leaking clipped observations.
    radius = int(math.ceil(3*cfg.sigma))
    kernel = np.ones((2*radius+1,)*2, np.uint8)
    rv = cv2.erode(((reference > 8) & (reference < 247)).astype(np.uint8), kernel).astype(bool)
    sv = cv2.erode(((source > 8) & (source < 247)).astype(np.uint8), kernel).astype(bool)
    forward = _fit(reference, source, rv, sv, cfg)
    if forward.accepted:
        reverse = _fit(source, reference, sv, rv, cfg)
        forward.fb_error = float(np.hypot(forward.dx+reverse.dx, forward.dy+reverse.dy))
        if not reverse.accepted or forward.fb_error > cfg.max_fb_error:
            forward.accepted = False
            forward.reason = "forward_reverse"
    return forward


def ecc_fit(reference, source, cfg=Config()):
    """Existing-style ECC fit only: NOT the full HachiCam production validator."""
    transform = np.eye(2, 3, dtype=np.float32)
    r = cv2.GaussianBlur(reference, (0, 0), cfg.sigma)
    s = cv2.GaussianBlur(source, (0, 0), cfg.sigma)
    mask = ((source > 8) & (source < 247)).astype(np.uint8)*255
    try:
        cc, transform = cv2.findTransformECC(r, s, transform, cv2.MOTION_TRANSLATION,
                    (cv2.TERM_CRITERIA_COUNT | cv2.TERM_CRITERIA_EPS, 30, .0001), mask, 3)
        dx, dy = map(float, transform[:, 2])
        accepted = np.isfinite(cc) and cc >= .65 and np.hypot(dx, dy) <= cfg.max_shift
        return Result(bool(accepted), "ecc_fit" if accepted else "ecc_rejected", dx, dy)
    except cv2.error:
        return Result(False, "ecc_failed")


def _guard_ecc(reference, source, cfg):
    fitted = ecc_fit(reference, source, cfg)
    if not fitted.accepted:
        return fitted
    fitted.accepted = False
    radius = int(math.ceil(3*cfg.sigma))
    kernel = np.ones((2*radius+1,)*2, np.uint8)
    rv = cv2.erode(((reference > 8) & (reference < 247)).astype(np.uint8), kernel).astype(bool)
    sv = cv2.erode(((source > 8) & (source < 247)).astype(np.uint8), kernel).astype(bool)
    margin = cfg.max_shift+4
    rv[:margin] = rv[-margin:] = False
    rv[:, :margin] = rv[:, -margin:] = False
    if rv.sum() < cfg.min_samples:
        fitted.reason = "insufficient_support"
        return fitted
    r = cv2.GaussianBlur(reference.astype(np.float32), (0, 0), cfg.sigma)
    s = cv2.GaussianBlur(source.astype(np.float32), (0, 0), cfg.sigma)
    gy, gx = np.gradient(r)
    _, fitted.eigen_min, fitted.eigen_ratio = _hessian(gx[rv], gy[rv])
    if fitted.eigen_min < cfg.min_eigenvalue or fitted.eigen_ratio < cfg.min_eigen_ratio:
        fitted.reason = "flat_or_aperture"
        return fitted
    return _assess(r, s, rv, sv, cfg, np.array([fitted.dx, fitted.dy]), fitted)


def guarded_ecc_fit(reference, source, cfg=Config()):
    """ECC + experimental diagnostics, still not the complete production gate."""
    forward = _guard_ecc(reference, source, cfg)
    if forward.accepted:
        reverse = _guard_ecc(source, reference, cfg)
        forward.fb_error = float(np.hypot(forward.dx+reverse.dx, forward.dy+reverse.dy))
        if not reverse.accepted or forward.fb_error > cfg.max_fb_error:
            forward.accepted = False
            forward.reason = "forward_reverse"
    return forward
