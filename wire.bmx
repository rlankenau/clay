Type Wire
	Field a:Box
	Field b:Box, bId:Int
	
	Function Make:Wire( a:Box, b:Box, bId:Int )
		Local wire:Wire = New Wire
		wire.a = a
		wire.b = b
		wire.bId = bId
		
		For Local other:Wire = EachIn patch.wires
			If other.b = b And other.bId = bId
				patch.wires.Remove( other )
			EndIf
		Next
		
		If a.done
			patch.sparks.AddLast( Spark.Make( wire ) )
		EndIf
		
		Return wire
	End Function

	Method Render()
		SetColor 238, 221, 238
		
		If a.done
			SetColor 255, 255, 0
		EndIf
		
		If _dragMode = DRAG_WIRE
			If patch.boxOver <> Null And patch.boxOver = b And patch.inOver = bId
				If Not CycleCheck( patch.boxOVer, from )
					SetColor 255, 0, 0
				EndIf
			EndIf
		ElseIf _dragMode = DRAG_NONE And patch.boxOver = b And patch.inOver = bId
			SetColor 255, 0, 0
		EndIf
		
		Local x0:Int = a.x + 3
		Local y0:Int = a.y + a.h - 2
		Local x1:Int = b.x + 3 + bId * b.gap
		Local y1:Int = b.y + 1
		DrawLine x0, y0, x1, y1
	End Method
	
	Function DrawFrom( from:Box, x1:Int, y1:Int )
		SetColor 255, 255, 255
		Local x0:Int = from.x + 3
		Local y0:Int = from.y + from.h - 2
		DrawLine x0, y0, x1, y1
	End Function
	
	Function DrawFromTo( a:Box, b:Box, bId:Int )
		SetColor 255, 255, 255
		
		If CycleCheck( b, a )
			SetColor 255, 0, 0
		EndIf
		
		Local x0:Int = a.x + 3
		Local y0:Int = a.y + a.h - 2
		Local x1:Int = b.x + 3 + bId * b.gap
		Local y1:Int = b.y + 1
		DrawLine x0, y0, x1, y1
	End Function
End Type