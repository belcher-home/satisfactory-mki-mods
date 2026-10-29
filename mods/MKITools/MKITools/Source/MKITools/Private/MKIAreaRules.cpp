#include "MKIAreaRules.h"

int32 FMKIAreaRules::ClampStep(int32 Step)
{
	return FMath::Clamp(Step, 1, MaxStep);
}

double FMKIAreaRules::EdgeCm(int32 Step)
{
	return ClampStep(Step) * StepCm;
}

FVector FMKIAreaRules::BottomCenter(const FBox& AimedBounds)
{
	const FVector Center = AimedBounds.GetCenter();
	return FVector(Center.X, Center.Y, AimedBounds.Min.Z);
}

FBox FMKIAreaRules::Cube(const FVector& BottomCenter, int32 Step)
{
	const double Edge = EdgeCm(Step);
	const double Half = Edge * 0.5;
	return FBox(BottomCenter - FVector(Half, Half, 0.0), BottomCenter + FVector(Half, Half, Edge));
}

FBox FMKIAreaRules::Interior(const FBox& Cube)
{
	return Cube.IsValid ? Cube.ExpandBy(-ReachCm) : Cube;
}

FBox FMKIAreaRules::SplineInterior(const FBox& Interior)
{
	if (!Interior.IsValid) return Interior;
	FBox Box = Interior;
	Box.Min.Z -= ReachCm + SplineBelowCm;
	return Box;
}

bool FMKIAreaRules::OrientedBoxReaches(const FBox& LocalBox, const FTransform& Transform, const FBox& Interior)
{
	if (!LocalBox.IsValid || !Interior.IsValid) return false;
	// The oriented box: its centre, half extents (scaled) and axes in world space.
	const FVector Scale = Transform.GetScale3D().GetAbs();
	const FVector Center = Transform.TransformPosition(LocalBox.GetCenter());
	const FVector Half = LocalBox.GetExtent() * Scale;
	const FQuat Rotation = Transform.GetRotation();
	const FVector Axes[3] = { Rotation.GetAxisX(), Rotation.GetAxisY(), Rotation.GetAxisZ() };
	const FVector BoxCenter = Interior.GetCenter();
	const FVector BoxHalf = Interior.GetExtent();
	const FVector World[3] = { FVector::XAxisVector, FVector::YAxisVector, FVector::ZAxisVector };
	const FVector Offset = Center - BoxCenter;
	// Separated along an axis when the centres are farther apart than the two projected half widths.
	auto Separated = [&](const FVector& Axis) -> bool
	{
		if (Axis.SizeSquared() < 1e-8) return false;
		const FVector Unit = Axis.GetUnsafeNormal();
		const double Oriented = Half.X * FMath::Abs(FVector::DotProduct(Axes[0], Unit)) + Half.Y * FMath::Abs(FVector::DotProduct(Axes[1], Unit)) + Half.Z * FMath::Abs(FVector::DotProduct(Axes[2], Unit));
		const double Aligned = BoxHalf.X * FMath::Abs(Unit.X) + BoxHalf.Y * FMath::Abs(Unit.Y) + BoxHalf.Z * FMath::Abs(Unit.Z);
		return FMath::Abs(FVector::DotProduct(Offset, Unit)) > Oriented + Aligned;
	};
	for (const FVector& Axis : World) if (Separated(Axis)) return false;
	for (const FVector& Axis : Axes) if (Separated(Axis)) return false;
	for (const FVector& A : World) for (const FVector& B : Axes) if (Separated(FVector::CrossProduct(A, B))) return false;
	return true;
}

bool FMKIAreaRules::PathReaches(TConstArrayView<FVector> Points, const FBox& Interior)
{
	if (!Interior.IsValid || Points.Num() == 0) return false;
	if (Points.Num() == 1) return Interior.IsInside(Points[0]);
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const FVector& Start = Points[Index - 1];
		const FVector& End = Points[Index];
		if (Interior.IsInside(Start) || Interior.IsInside(End)) return true;
		const FVector Direction = End - Start;
		if (!Direction.IsNearlyZero() && FMath::LineBoxIntersection(Interior, Start, End, Direction)) return true;
	}
	return false;
}

void FMKIAreaRules::SortTopDown(TArray<TPair<double, int32>>& TopsAndIndices)
{
	// Stable, so pieces at the same height keep the order they were found in.
	TopsAndIndices.StableSort([](const TPair<double, int32>& A, const TPair<double, int32>& B) { return A.Key > B.Key; });
}
