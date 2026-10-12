package com.scanner.app.ui.camera

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.animateOffsetAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.PathEffect
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.unit.dp
import com.scanner.app.domain.model.DetectionResult

@Composable
fun EdgeOverlay(
    detectionResult: DetectionResult,
    isCurvedMode: Boolean,
    modifier: Modifier = Modifier
) {
    val isFound = detectionResult.found && detectionResult.points.isNotEmpty()
    val animatedAlpha by animateFloatAsState(
        targetValue = if (isFound) 1f else 0f,
        animationSpec = tween(durationMillis = 150),
        label = "alphaAnim"
    )

    val points = detectionResult.points

    // Remember last valid 4 points for smooth exit animation
    var targetP0 by remember { mutableStateOf(Offset.Zero) }
    var targetP1 by remember { mutableStateOf(Offset.Zero) }
    var targetP2 by remember { mutableStateOf(Offset.Zero) }
    var targetP3 by remember { mutableStateOf(Offset.Zero) }
    var targetPartial by remember { mutableStateOf(false) }

    if (isFound && points.size >= 4) {
        targetPartial = detectionResult.partial
        targetP0 = Offset(points[0].x, points[0].y)
        targetP1 = Offset(points[1].x, points[1].y)
        targetP2 = Offset(points[2].x, points[2].y)
        targetP3 = Offset(points[3].x, points[3].y)
    }

    val animP0 by animateOffsetAsState(targetValue = targetP0, animationSpec = tween(durationMillis = 70), label = "p0")
    val animP1 by animateOffsetAsState(targetValue = targetP1, animationSpec = tween(durationMillis = 70), label = "p1")
    val animP2 by animateOffsetAsState(targetValue = targetP2, animationSpec = tween(durationMillis = 70), label = "p2")
    val animP3 by animateOffsetAsState(targetValue = targetP3, animationSpec = tween(durationMillis = 70), label = "p3")

    if (animatedAlpha <= 0.01f) return

    val strokeColor = if (targetPartial) Color(0xFFFFBB44) else if (isCurvedMode) Color(0xFF00B0FF) else Color(0xFF00E676)
    val cornerColor = Color.White

    Canvas(modifier = modifier.fillMaxSize()) {
        val frameW = if (detectionResult.frameWidth > 0) detectionResult.frameWidth.toFloat() else size.width
        val frameH = if (detectionResult.frameHeight > 0) detectionResult.frameHeight.toFloat() else size.height

        val scale = minOf(size.width / frameW, size.height / frameH)
        val offsetX = (size.width - frameW * scale) / 2f
        val offsetY = (size.height - frameH * scale) / 2f

        fun mapX(x: Float) = x * scale + offsetX
        fun mapY(y: Float) = y * scale + offsetY

        clipRect(offsetX, offsetY, offsetX + frameW * scale, offsetY + frameH * scale) {
        if (!isCurvedMode) {
            val s0 = Offset(mapX(animP0.x), mapY(animP0.y))
            val s1 = Offset(mapX(animP1.x), mapY(animP1.y))
            val s2 = Offset(mapX(animP2.x), mapY(animP2.y))
            val s3 = Offset(mapX(animP3.x), mapY(animP3.y))

            val path = Path().apply {
                moveTo(s0.x, s0.y)
                lineTo(s1.x, s1.y)
                lineTo(s2.x, s2.y)
                lineTo(s3.x, s3.y)
                close()
            }

            // Outline stroke ONLY - NO FILL to prevent screen discoloration / flashing
            drawPath(
                path = path,
                color = strokeColor.copy(alpha = animatedAlpha),
                style = Stroke(width = 2.5.dp.toPx(), pathEffect = if (targetPartial)
                    PathEffect.dashPathEffect(floatArrayOf(10.dp.toPx(), 7.dp.toPx())) else null)
            )

            // Sleek corner markers
            listOf(s0, s1, s2, s3).forEach { corner ->
                drawCircle(
                    color = strokeColor.copy(alpha = animatedAlpha),
                    radius = 7.dp.toPx(),
                    center = corner
                )
                drawCircle(
                    color = cornerColor.copy(alpha = animatedAlpha),
                    radius = 4.5.dp.toPx(),
                    center = corner
                )
            }
        } else {
            val boundaryPoints = detectionResult.boundaryPoints ?: points
            if (boundaryPoints.isNotEmpty()) {
                val path = Path().apply {
                    moveTo(mapX(boundaryPoints[0].x), mapY(boundaryPoints[0].y))
                    for (i in 1 until boundaryPoints.size - 1) {
                        val p1 = boundaryPoints[i]
                        val p2 = boundaryPoints[i + 1]
                        val midX = (mapX(p1.x) + mapX(p2.x)) / 2f
                        val midY = (mapY(p1.y) + mapY(p2.y)) / 2f
                        quadraticTo(mapX(p1.x), mapY(p1.y), midX, midY)
                    }
                    lineTo(mapX(boundaryPoints.last().x), mapY(boundaryPoints.last().y))
                    close()
                }

                // Outline stroke ONLY
                drawPath(
                    path = path,
                    color = strokeColor.copy(alpha = animatedAlpha),
                    style = Stroke(width = 2.5.dp.toPx())
                )
            }
        }
        }
    }
}
